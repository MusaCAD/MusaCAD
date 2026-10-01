// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/update_installer.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QStandardPaths>
#include <QUrl>

#include <string>

#include "musacad/ui/update_channel.hpp"

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <softpub.h>
#include <wintrust.h>
#endif

namespace musacad::ui {

namespace {

constexpr int kSmallTimeoutMs = 20'000;

/// Only the release's own hosts, and only over TLS: github.com serves the release page
/// and the download link, which redirects to GitHub's file hosts under
/// githubusercontent.com. Anything else -- another host, plain http -- is refused, for
/// the first request and for every redirect.
bool allowed_url(const QUrl& url) {
    if (url.scheme() != QLatin1String("https")) {
        return false;
    }
    const QString host = url.host().toLower();
    return host == QLatin1String("github.com") || host.endsWith(QLatin1String(".github.com")) ||
           host.endsWith(QLatin1String(".githubusercontent.com"));
}

QString user_agent() {
    return QStringLiteral("MusaCAD/%1 (update)").arg(UpdateChecker::current_version());
}

#if defined(Q_OS_WIN)
/// The version in the file's VS_FIXEDFILEINFO ("0.6.0"; "" when the file has no version
/// resource). Language-independent, unlike the string table.
QString product_version_of(const QString& path) {
    const std::wstring w = QDir::toNativeSeparators(path).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(w.c_str(), &handle);
    if (size == 0) {
        return {};
    }
    std::string buf(size, '\0');
    if (!GetFileVersionInfoW(w.c_str(), 0, size, buf.data())) {
        return {};
    }
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &len) || ffi == nullptr ||
        len < sizeof(VS_FIXEDFILEINFO)) {
        return {};
    }
    return QStringLiteral("%1.%2.%3")
        .arg(HIWORD(ffi->dwProductVersionMS))
        .arg(LOWORD(ffi->dwProductVersionMS))
        .arg(HIWORD(ffi->dwProductVersionLS));
}

/// A Windows executable: the DOS stub's "MZ" and a "PE\0\0" header where e_lfanew points.
bool is_pe_executable(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray head = f.read(64);
    if (head.size() < 64 || head[0] != 'M' || head[1] != 'Z') {
        return false;
    }
    const auto e_lfanew = static_cast<quint32>(static_cast<quint8>(head[60])) |
                          (static_cast<quint32>(static_cast<quint8>(head[61])) << 8) |
                          (static_cast<quint32>(static_cast<quint8>(head[62])) << 16) |
                          (static_cast<quint32>(static_cast<quint8>(head[63])) << 24);
    if (e_lfanew < 64 || e_lfanew > 4096 || !f.seek(e_lfanew)) {
        return false;
    }
    const QByteArray sig = f.read(4);
    return sig == QByteArray("PE\0\0", 4);
}

/// Authenticode: a signature that chains to a trusted root, on the whole file. No UI,
/// no revocation lookups (a machine without network must still be able to update).
bool has_valid_signature(const QString& path) {
    const std::wstring w = QDir::toNativeSeparators(path).toStdWString();
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = w.c_str();
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_SAFER_FLAG;
    const LONG status = WinVerifyTrust(nullptr, &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &data);
    return status == ERROR_SUCCESS;
}
#endif

} // namespace

UpdateInstaller::UpdateInstaller(QObject* parent) : QObject(parent), net_(new QNetworkAccessManager(this)) {}

UpdateInstaller::~UpdateInstaller() {
    if (reply_ != nullptr) {
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    if (out_ != nullptr) {
        out_->close();
        out_->remove();
        delete out_;
        out_ = nullptr;
    }
}

bool UpdateInstaller::supported() {
#if defined(Q_OS_WIN)
    return UpdateChecker::detect_channel() == update::Channel::WindowsInstaller;
#else
    return false;
#endif
}

QString UpdateInstaller::downloads_dir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/updates");
}

QString UpdateInstaller::install_dir() {
    return QCoreApplication::applicationDirPath();
}

bool UpdateInstaller::self_is_signed() {
#if defined(Q_OS_WIN)
    return has_valid_signature(QCoreApplication::applicationFilePath());
#else
    return false;
#endif
}

bool UpdateInstaller::verify_installer(const QString& path, const QString& version, qint64 expected_size,
                                       const QString& expected_sha256, bool require_signature, QString& err) {
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        err = QStringLiteral("The downloaded installer is missing (%1).").arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (expected_size > 0 && fi.size() != expected_size) {
        err = QStringLiteral("The download is %1 bytes; the release says %2. It was not run.")
                  .arg(fi.size())
                  .arg(expected_size);
        return false;
    }
    if (!expected_sha256.isEmpty()) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            err = QStringLiteral("Could not read the download to check it.");
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&f)) {
            err = QStringLiteral("Could not read the download to check it.");
            return false;
        }
        const QString actual = QString::fromLatin1(hash.result().toHex());
        if (actual != expected_sha256.toLower()) {
            err = QStringLiteral("The download's SHA-256 does not match the one the release published. "
                                 "It was not run.");
            return false;
        }
    }
#if defined(Q_OS_WIN)
    if (!is_pe_executable(path)) {
        err = QStringLiteral("The download is not a Windows program. It was not run.");
        return false;
    }
    const QString have = product_version_of(path);
    if (have.isEmpty() || !(update::parse_version(have.toStdString()) == update::parse_version(version.toStdString()))) {
        err = QStringLiteral("The download says it is version \"%1\", not the %2 the release announced. "
                             "It was not run.")
                  .arg(have.isEmpty() ? QStringLiteral("unknown") : have, version);
        return false;
    }
    if (require_signature && !has_valid_signature(path)) {
        err = QStringLiteral("This copy of Musa CAD is signed, but the downloaded installer carries no valid "
                             "signature. It was not run.");
        return false;
    }
    return true;
#else
    Q_UNUSED(version);
    Q_UNUSED(require_signature);
    err = QStringLiteral("In-place updates are only supported on Windows.");
    return false;
#endif
}

bool UpdateInstaller::launch(const QString& installer, const QString& install_dir, QString& err) {
#if defined(Q_OS_WIN)
    // /S: silent. /UPDATE: the installer waits for this program to exit, updates in place
    // and starts the new version as the user. /D=<dir>: the installation to update; NSIS
    // reads it to the end of the line, unquoted, so it must come last.
    const QString dir = QDir::toNativeSeparators(QDir::cleanPath(install_dir));
    if (dir.contains(QLatin1Char('"')) || dir.contains(QLatin1Char('\n'))) {
        err = QStringLiteral("The installation folder's name cannot be passed to the installer.");
        return false;
    }
    const std::wstring file = QDir::toNativeSeparators(installer).toStdWString();
    const std::wstring params = (QStringLiteral("/S /UPDATE /D=") + dir).toStdWString();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas"; // the installer needs administrator rights: Windows asks
    sei.lpFile = file.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        const DWORD code = GetLastError();
        err = code == ERROR_CANCELLED
                  ? QStringLiteral("The update was not allowed to run as administrator, so nothing was changed.")
                  : QStringLiteral("The installer could not be started (Windows error %1).").arg(code);
        return false;
    }
    if (sei.hProcess != nullptr) {
        CloseHandle(sei.hProcess);
    }
    return true;
#else
    Q_UNUSED(installer);
    Q_UNUSED(install_dir);
    err = QStringLiteral("In-place updates are only supported on Windows.");
    return false;
#endif
}

void UpdateInstaller::remove_stale_downloads() {
    const QDir dir(downloads_dir());
    if (!dir.exists()) {
        return;
    }
    const std::string current = UpdateChecker::current_version().toStdString();
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files)) {
        const QString name = fi.fileName();
        if (name.endsWith(QLatin1String(".part"))) {
            QFile::remove(fi.absoluteFilePath()); // an interrupted download
            continue;
        }
        const std::string v = update::version_in_asset_name(name.toStdString());
        if (!v.empty() && !update::is_newer(v, current)) {
            QFile::remove(fi.absoluteFilePath()); // applied, or older than what runs now
        }
    }
}

QNetworkReply* UpdateInstaller::get(const QString& url) {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, user_agent());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    req.setTransferTimeout(kSmallTimeoutMs);
    QNetworkReply* reply = net_->get(req);
    // Every hop is checked, not just the first address.
    connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl& to) {
        if (allowed_url(to)) {
            Q_EMIT reply->redirectAllowed();
        } else {
            reply->abort();
        }
    });
    return reply;
}

void UpdateInstaller::fail(const QString& why) {
    if (done_) {
        return;
    }
    done_ = true;
    if (out_ != nullptr) {
        out_->close();
        out_->remove();
        delete out_;
        out_ = nullptr;
    }
    Q_EMIT finished(false, why);
}

void UpdateInstaller::start(const UpdateChecker::Result& update) {
    done_ = false;
    cancelled_ = false;
    update_ = update;
    sha256_.clear();
    if (!supported()) {
        fail(QStringLiteral("This copy of Musa CAD cannot update itself; download the new version from %1.")
                 .arg(update.release_page));
        return;
    }
    if (update.download_url.isEmpty() || update.download_name.isEmpty() || update.latest.isEmpty()) {
        fail(QStringLiteral("The release carries no installer for this computer."));
        return;
    }
    if (!allowed_url(QUrl(update.download_url)) ||
        (!update.checksum_url.isEmpty() && !allowed_url(QUrl(update.checksum_url)))) {
        fail(QStringLiteral("The update is not hosted where Musa CAD releases live; it was not downloaded."));
        return;
    }
    if (!QSslSocket::supportsSsl()) {
        fail(QStringLiteral("This build of Musa CAD cannot make secure (https) connections. Download the new "
                            "version from %1.")
                 .arg(update.release_page));
        return;
    }
    if (!QDir().mkpath(downloads_dir())) {
        fail(QStringLiteral("Could not create %1.").arg(QDir::toNativeSeparators(downloads_dir())));
        return;
    }
    target_ = downloads_dir() + QStringLiteral("/") + update.download_name;
    if (update.checksum_url.isEmpty()) {
        fetch_installer();
    } else {
        fetch_checksum();
    }
}

void UpdateInstaller::cancel() {
    cancelled_ = true;
    if (reply_ != nullptr) {
        reply_->abort();
    } else {
        fail(QStringLiteral("Cancelled."));
    }
}

void UpdateInstaller::fetch_checksum() {
    Q_EMIT stage(QStringLiteral("Reading the release's checksum…"));
    reply_ = get(update_.checksum_url);
    connect(reply_, &QNetworkReply::finished, this, [this] {
        QNetworkReply* r = reply_;
        reply_ = nullptr;
        r->deleteLater();
        if (cancelled_) {
            fail(QStringLiteral("Cancelled."));
            return;
        }
        if (r->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("The release's checksum could not be read: %1").arg(r->errorString()));
            return;
        }
        const std::string hex =
            update::parse_sha256_sums(QString::fromUtf8(r->readAll()).toStdString(), update_.download_name.toStdString());
        if (hex.empty()) {
            fail(QStringLiteral("The release's checksum file names no SHA-256 for %1.").arg(update_.download_name));
            return;
        }
        sha256_ = QString::fromStdString(hex);
        fetch_installer();
    });
}

void UpdateInstaller::fetch_installer() {
    // A download that already passed every check -- an update postponed a moment ago,
    // or one whose installer could not start -- is used as it is.
    if (QFileInfo::exists(target_)) {
        QString err;
        if (verify_installer(target_, update_.latest, update_.download_size, sha256_, self_is_signed(), err)) {
            done_ = true;
            Q_EMIT finished(true, target_);
            return;
        }
        QFile::remove(target_);
    }
    out_ = new QFile(target_ + QStringLiteral(".part"));
    if (!out_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("Could not write %1.").arg(QDir::toNativeSeparators(out_->fileName())));
        return;
    }
    Q_EMIT stage(QStringLiteral("Downloading %1…").arg(update_.download_name));
    reply_ = get(update_.download_url);
    reply_->setReadBufferSize(0);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (out_ != nullptr && reply_ != nullptr) {
            out_->write(reply_->readAll());
        }
    });
    connect(reply_, &QNetworkReply::downloadProgress, this,
            [this](qint64 done, qint64 total) { Q_EMIT progress(done, total); });
    connect(reply_, &QNetworkReply::finished, this, [this] {
        QNetworkReply* r = reply_;
        reply_ = nullptr;
        r->deleteLater();
        if (cancelled_) {
            fail(QStringLiteral("Cancelled."));
            return;
        }
        if (r->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("The download failed: %1").arg(r->errorString()));
            return;
        }
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            fail(QStringLiteral("The download server answered %1 for %2.").arg(status).arg(update_.download_name));
            return;
        }
        if (out_ != nullptr) {
            out_->write(r->readAll());
            out_->close();
        }
        QFile::remove(target_);
        if (out_ == nullptr || !out_->rename(target_)) {
            fail(QStringLiteral("Could not place the download at %1.").arg(QDir::toNativeSeparators(target_)));
            return;
        }
        delete out_;
        out_ = nullptr;
        verify_and_finish();
    });
}

void UpdateInstaller::verify_and_finish() {
    Q_EMIT stage(QStringLiteral("Checking the download…"));
    QString err;
    if (!verify_installer(target_, update_.latest, update_.download_size, sha256_, self_is_signed(), err)) {
        QFile::remove(target_);
        fail(err);
        return;
    }
    done_ = true;
    Q_EMIT finished(true, target_);
}

} // namespace musacad::ui
