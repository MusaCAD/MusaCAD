// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/dwg_installer.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSslSocket>
#include <QStandardPaths>
#include <QSysInfo>

namespace musacad::ui {

namespace {
constexpr int kPageTimeoutMs = 20'000;
constexpr int kUnpackTimeoutMs = 600'000; // the AppImage / MSI / DMG unpack

QString user_agent() {
    return QStringLiteral("MusaCAD/%1").arg(QCoreApplication::applicationVersion());
}

QNetworkRequest request_for(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, user_agent());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kPageTimeoutMs);
    return req;
}

// The program inside an unpacked install, per platform (empty when absent).
QString program_in(const QString& dir) {
#if defined(Q_OS_WIN)
    QDirIterator it(dir, {QStringLiteral("ODAFileConverter.exe")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        return it.next();
    }
    return {};
#elif defined(Q_OS_MACOS)
    const QString p = dir + QStringLiteral("/ODAFileConverter.app/Contents/MacOS/ODAFileConverter");
    return QFileInfo::exists(p) ? p : QString();
#else
    // AppRun is a link to usr/bin/ODAFileConverter; the binary finds its libraries from
    // its own location, so it is run directly (its name also tells DwgConverter the kind).
    const QString p = dir + QStringLiteral("/squashfs-root/usr/bin/ODAFileConverter");
    return QFileInfo::exists(p) ? p : QString();
#endif
}

bool run(const QString& program, const QStringList& args, const QString& cwd, QString& err) {
    QProcess proc;
    proc.setWorkingDirectory(cwd);
    proc.start(program, args);
    if (!proc.waitForStarted(20'000)) {
        err = QStringLiteral("Could not start %1: %2").arg(program, proc.errorString());
        return false;
    }
    if (!proc.waitForFinished(kUnpackTimeoutMs)) {
        proc.kill();
        proc.waitForFinished(2'000);
        err = QStringLiteral("%1 did not finish in time.").arg(program);
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString tail = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        err = QStringLiteral("%1 exited with code %2. %3").arg(program).arg(proc.exitCode()).arg(tail);
        return false;
    }
    return true;
}
} // namespace

QString OdaInstaller::install_dir() {
    const QString forced = qEnvironmentVariable("MUSACAD_ODA_DIR");
    if (!forced.isEmpty()) {
        return forced;
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/converters/oda");
}

QString OdaInstaller::installed_program() {
    return program_in(install_dir());
}

bool OdaInstaller::remove_installed(QString& err) {
    const QString dir = install_dir();
    const QString program = program_in(dir);
    if (!program.isEmpty() &&
        QSettings().value(QStringLiteral("io/dwg_converter_path")).toString() == program) {
        QSettings().remove(QStringLiteral("io/dwg_converter_path"));
    }
    if (!QDir(dir).exists()) {
        return true;
    }
    if (!QDir(dir).removeRecursively()) {
        err = QStringLiteral("Could not remove %1.").arg(dir);
        return false;
    }
    return true;
}

QUrl OdaInstaller::page_url() {
    return QUrl(QStringLiteral("https://www.opendesign.com/guestfiles/oda_file_converter"));
}

QUrl OdaInstaller::download_url(const QString& filename) {
    QUrl u(QStringLiteral("https://www.opendesign.com/guestfiles/get"));
    u.setQuery(QStringLiteral("filename=") + filename);
    return u;
}

QString OdaInstaller::platform_pattern() {
    const QString cpu = QSysInfo::currentCpuArchitecture();
#if defined(Q_OS_WIN)
    if (cpu == QLatin1String("x86_64")) {
        return QStringLiteral(R"(ODAFileConverter_QT6_vc16_amd64dll_(\d+\.\d+)\.msi)");
    }
#elif defined(Q_OS_MACOS)
    if (cpu == QLatin1String("arm64")) {
        return QStringLiteral(R"(ODAFileConverter_QT6_macOsX_arm64_[\w.]+?_(\d+\.\d+)\.dmg)");
    }
    if (cpu == QLatin1String("x86_64")) {
        return QStringLiteral(R"(ODAFileConverter_QT6_macOsX_x64_[\w.]+?_(\d+\.\d+)\.dmg)");
    }
#elif defined(Q_OS_LINUX)
    if (cpu == QLatin1String("x86_64")) {
        return QStringLiteral(R"(ODAFileConverter_QT6_lnxX64_[\w.]+?_(\d+\.\d+)\.AppImage)");
    }
#endif
    return {};
}

std::optional<OdaInstaller::Release> OdaInstaller::parse_release(const QByteArray& page_html) {
    const QString pattern = platform_pattern();
    if (pattern.isEmpty()) {
        return std::nullopt;
    }
    const QRegularExpression re(pattern);
    const QRegularExpressionMatch m = re.match(QString::fromUtf8(page_html));
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    Release r;
    r.filename = m.captured(0);
    r.version = m.captured(1);
    return r;
}

OdaInstaller::Release OdaInstaller::fallback_release() {
    // The files on the page on 2026-09-23 (sizes as served then).
    Release r;
    r.version = QStringLiteral("27.1");
#if defined(Q_OS_WIN)
    r.filename = QStringLiteral("ODAFileConverter_QT6_vc16_amd64dll_27.1.msi");
    r.size_hint = 28'812'288;
#elif defined(Q_OS_MACOS)
    if (QSysInfo::currentCpuArchitecture() == QLatin1String("arm64")) {
        r.filename = QStringLiteral("ODAFileConverter_QT6_macOsX_arm64_15.0dll_27.1.dmg");
        r.size_hint = 56'224'716;
    } else {
        r.filename = QStringLiteral("ODAFileConverter_QT6_macOsX_x64_15.0dll_27.1.dmg");
        r.size_hint = 62'591'966;
    }
#else
    r.filename = QStringLiteral("ODAFileConverter_QT6_lnxX64_8.3dll_27.1.AppImage");
    r.size_hint = 85'128'384;
#endif
    if (platform_pattern().isEmpty()) {
        r.filename.clear(); // no build for this platform
    }
    return r;
}

bool OdaInstaller::install_from_file(const QString& archive, QString& program, QString& err) {
    const QString dir = install_dir();
    if (!QDir().mkpath(dir)) {
        err = QStringLiteral("Could not create %1.").arg(dir);
        return false;
    }
    if (!QFileInfo::exists(archive)) {
        err = QStringLiteral("%1 does not exist.").arg(archive);
        return false;
    }
#if defined(Q_OS_WIN)
    // An administrative install lays the files out under TARGETDIR without installing
    // anything (no registry, no elevation).
    const QString target = QDir::toNativeSeparators(dir + QStringLiteral("/msi"));
    QDir(target).removeRecursively();
    if (!run(QStringLiteral("msiexec"),
             {QStringLiteral("/a"), QDir::toNativeSeparators(archive), QStringLiteral("/qn"),
              QStringLiteral("TARGETDIR=") + target},
             dir, err)) {
        return false;
    }
#elif defined(Q_OS_MACOS)
    const QString mnt = dir + QStringLiteral("/mnt");
    QDir().mkpath(mnt);
    if (!run(QStringLiteral("hdiutil"),
             {QStringLiteral("attach"), QStringLiteral("-nobrowse"), QStringLiteral("-readonly"),
              QStringLiteral("-mountpoint"), mnt, archive},
             dir, err)) {
        return false;
    }
    QDir(dir + QStringLiteral("/ODAFileConverter.app")).removeRecursively();
    QString app;
    for (const QFileInfo& fi : QDir(mnt).entryInfoList({QStringLiteral("*.app")}, QDir::Dirs)) {
        app = fi.absoluteFilePath();
        break;
    }
    bool copied = false;
    if (!app.isEmpty()) {
        copied = run(QStringLiteral("ditto"), {app, dir + QStringLiteral("/ODAFileConverter.app")}, dir, err);
    } else {
        err = QStringLiteral("The disk image holds no application bundle.");
    }
    QString detach_err;
    run(QStringLiteral("hdiutil"), {QStringLiteral("detach"), mnt}, dir, detach_err);
    if (!copied) {
        return false;
    }
#else
    // The AppImage unpacks itself into ./squashfs-root (type-2 runtime; no FUSE needed).
    QDir(dir + QStringLiteral("/squashfs-root")).removeRecursively();
    QFile f(archive);
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    if (!run(archive, {QStringLiteral("--appimage-extract")}, dir, err)) {
        return false;
    }
#endif
    program = program_in(dir);
    if (program.isEmpty()) {
        err = QStringLiteral("The download unpacked, but no ODAFileConverter program was found in %1.").arg(dir);
        return false;
    }
    // The archive is not needed once unpacked (the AppImage alone is 85 MB).
    if (QFileInfo(archive).absolutePath().startsWith(QFileInfo(dir).absoluteFilePath())) {
        QFile::remove(archive);
    }
    return true;
}

OdaInstaller::OdaInstaller(QObject* parent) : QObject(parent), net_(new QNetworkAccessManager(this)) {}

OdaInstaller::~OdaInstaller() {
    if (reply_ != nullptr) {
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    delete out_;
}

void OdaInstaller::fail(const QString& why) {
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

void OdaInstaller::start() {
    done_ = false;
    cancelled_ = false;
    if (platform_pattern().isEmpty()) {
        fail(QStringLiteral("The Open Design Alliance publishes no ODA File Converter for this platform "
                            "(%1 %2).")
                 .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()));
        return;
    }
    if (!QSslSocket::supportsSsl()) {
        fail(QStringLiteral("This build of Musa CAD cannot make secure (https) connections, so it cannot "
                            "download the converter. Download it from %1 and Browse to it in DWG Setup.")
                 .arg(page_url().toString()));
        return;
    }
    fetch_page();
}

void OdaInstaller::cancel() {
    cancelled_ = true;
    if (reply_ != nullptr) {
        reply_->abort();
    } else {
        fail(QStringLiteral("Cancelled."));
    }
}

void OdaInstaller::fetch_page() {
    Q_EMIT stage(QStringLiteral("Reading the download page…"));
    reply_ = net_->get(request_for(page_url()));
    connect(reply_, &QNetworkReply::finished, this, [this] {
        QNetworkReply* r = reply_;
        reply_ = nullptr;
        r->deleteLater();
        if (cancelled_) {
            fail(QStringLiteral("Cancelled."));
            return;
        }
        std::optional<Release> rel;
        if (r->error() == QNetworkReply::NoError) {
            rel = parse_release(r->readAll());
        }
        release_ = rel ? *rel : fallback_release(); // the page unreadable: the known release
        if (rel) {
            release_.size_hint = fallback_release().version == release_.version
                                     ? fallback_release().size_hint
                                     : 0;
        }
        fetch_file();
    });
}

void OdaInstaller::fetch_file() {
    const QString dl = install_dir() + QStringLiteral("/download");
    if (!QDir().mkpath(dl)) {
        fail(QStringLiteral("Could not create %1.").arg(dl));
        return;
    }
    target_ = dl + QStringLiteral("/") + release_.filename;
    out_ = new QFile(target_ + QStringLiteral(".part"));
    if (!out_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("Could not write %1.").arg(out_->fileName()));
        return;
    }
    Q_EMIT stage(QStringLiteral("Downloading %1 (%2)…").arg(release_.filename, release_.version));
    QNetworkRequest req = request_for(download_url(release_.filename));
    req.setTransferTimeout(0); // a large file on a slow line: no idle timeout
    reply_ = net_->get(req);
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
            fail(QStringLiteral("The download server answered %1 for %2.").arg(status).arg(release_.filename));
            return;
        }
        if (out_ != nullptr) {
            out_->write(r->readAll());
            out_->close();
        }
        QFile::remove(target_);
        if (out_ == nullptr || !out_->rename(target_)) {
            fail(QStringLiteral("Could not place the downloaded file at %1.").arg(target_));
            return;
        }
        delete out_;
        out_ = nullptr;
        done_ = true;
        Q_EMIT finished(true, target_);
    });
}

} // namespace musacad::ui
