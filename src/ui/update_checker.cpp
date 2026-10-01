// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/update_checker.hpp"

#include <string>
#include <utility>
#include <vector>

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "musacad/core/version.hpp"
#include "musacad/ui/update_installer.hpp"

namespace musacad::ui {

namespace {

constexpr int kTimeoutMs = 15'000;
const QString kFlathubAppId = QStringLiteral("org.musacad.MusaCAD");
const QString kFlathubApi = QStringLiteral("https://flathub.org/api/v2/appstream/org.musacad.MusaCAD");
const QString kLatestRelease = QStringLiteral("https://api.github.com/repos/MusaCAD/MusaCAD/releases/latest");
const QString kReleasesPage = QStringLiteral("https://github.com/MusaCAD/MusaCAD/releases");

QString release_page_for(const QString& version) {
    return kReleasesPage + QStringLiteral("/tag/v") + version;
}

QString strip_v(QString v) {
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V'))) {
        v.remove(0, 1);
    }
    return v;
}

QNetworkRequest request_for(const QString& url) {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("MusaCAD/%1 (update check)").arg(UpdateChecker::current_version()));
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kTimeoutMs);
    return req;
}

/// Flathub's appstream record: the newest version among its releases.
UpdateChecker::Result parse_flathub(const QJsonObject& root) {
    UpdateChecker::Result r;
    QString best;
    for (const QJsonValue& rel : root.value(QStringLiteral("releases")).toArray()) {
        const QString v = rel.toObject().value(QStringLiteral("version")).toString();
        if (best.isEmpty() || update::is_newer(v.toStdString(), best.toStdString())) {
            best = v;
        }
    }
    if (best.isEmpty()) {
        r.error = QStringLiteral("Flathub listed no releases.");
        return r;
    }
    r.ok = true;
    r.latest = strip_v(best);
    r.release_page = release_page_for(r.latest);
    return r;
}

/// GitHub's latest release: its tag, its page, and the file for this package.
UpdateChecker::Result parse_github(const QJsonObject& root, update::Channel channel) {
    UpdateChecker::Result r;
    const QString tag = root.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty()) {
        r.error = QStringLiteral("The release has no version tag.");
        return r;
    }
    r.ok = true;
    r.latest = strip_v(tag);
    r.release_page = root.value(QStringLiteral("html_url")).toString(release_page_for(r.latest));
    std::vector<std::pair<std::string, std::string>> assets;
    for (const QJsonValue& a : root.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject o = a.toObject();
        assets.emplace_back(o.value(QStringLiteral("name")).toString().toStdString(),
                            o.value(QStringLiteral("browser_download_url")).toString().toStdString());
    }
    r.download_url = QString::fromStdString(
        update::pick_asset(assets, channel, QSysInfo::currentCpuArchitecture().toStdString()));
    if (r.download_url.isEmpty()) {
        return r;
    }
    // The file's name and size, and the SHA-256 published beside it (either its own
    // "<name>.sha256" or a release-wide SHA256SUMS), for the in-place update to check.
    QString sums_url;
    for (const QJsonValue& a : root.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject o = a.toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        const QString url = o.value(QStringLiteral("browser_download_url")).toString();
        if (url == r.download_url) {
            r.download_name = name;
            r.download_size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble());
        } else if (name == QStringLiteral("SHA256SUMS") || name == QStringLiteral("SHA256SUMS.txt")) {
            sums_url = url;
        }
    }
    const QString own = QString::fromStdString(update::checksum_asset_name(r.download_name.toStdString()));
    for (const QJsonValue& a : root.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject o = a.toObject();
        if (o.value(QStringLiteral("name")).toString() == own) {
            r.checksum_url = o.value(QStringLiteral("browser_download_url")).toString();
        }
    }
    if (r.checksum_url.isEmpty()) {
        r.checksum_url = sums_url;
    }
    return r;
}

} // namespace

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent), net_(new QNetworkAccessManager(this)) {}

update::Channel UpdateChecker::detect_channel() {
    if (QFileInfo::exists(QStringLiteral("/.flatpak-info")) || !qEnvironmentVariable("FLATPAK_ID").isEmpty()) {
        return update::Channel::Flatpak;
    }
    if (!qEnvironmentVariable("APPIMAGE").isEmpty()) {
        return update::Channel::AppImage;
    }
#if defined(Q_OS_WIN)
    // The installer leaves its uninstaller beside the program; a build tree has none.
    if (QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/uninstall.exe"))) {
        return update::Channel::WindowsInstaller;
    }
#elif defined(Q_OS_MACOS)
    if (QCoreApplication::applicationDirPath().contains(QStringLiteral(".app/Contents/MacOS"))) {
        return update::Channel::MacDmg;
    }
#endif
    return update::Channel::SourceBuild;
}

QString UpdateChecker::current_version() {
    const std::string_view v = core::version_string();
    return QString::fromUtf8(v.data(), static_cast<int>(v.size()));
}

void UpdateChecker::check() {
    if (busy_) {
        return;
    }
    busy_ = true;
    const update::Channel channel = detect_channel();
    const bool flathub = channel == update::Channel::Flatpak;
    QNetworkReply* reply = net_->get(request_for(flathub ? kFlathubApi : kLatestRelease));
    connect(reply, &QNetworkReply::finished, this, [this, reply, flathub, channel] {
        reply->deleteLater();
        Result r;
        if (reply->error() != QNetworkReply::NoError) {
            r.error = reply->errorString();
            finish(std::move(r));
            return;
        }
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            r.error = QStringLiteral("The update server sent an unreadable answer.");
            finish(std::move(r));
            return;
        }
        finish(flathub ? parse_flathub(doc.object()) : parse_github(doc.object(), channel));
    });
}

void UpdateChecker::finish(Result result) {
    busy_ = false;
    Q_EMIT finished(result);
}

void show_update_dialog(QWidget* parent, const UpdateChecker::Result& result, update::Channel channel,
                        std::function<void()> on_skip, std::function<bool()> prepare_for_install) {
    auto* dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModal(false);
    dlg->setWindowTitle(QStringLiteral("Update Available"));
    auto* lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(22, 20, 22, 16);
    lay->setSpacing(12);

    // Header: the mark, the new version, and a link to what changed.
    auto* head = new QHBoxLayout;
    head->setSpacing(14);
    auto* logo = new QLabel(dlg);
    logo->setPixmap(QIcon(QStringLiteral(":/branding/musacad_logo.svg")).pixmap(44, 44));
    logo->setAlignment(Qt::AlignTop);
    head->addWidget(logo);
    auto* title = new QLabel(
        QStringLiteral("<div style='font-size:13pt; font-weight:600'>Musa CAD %1 is available</div>"
                       "<div style='margin-top:4px'>You have version %2. "
                       "<a href='%3' style='color:#0bd1b5'>See what's new</a></div>")
            .arg(result.latest.toHtmlEscaped(), UpdateChecker::current_version().toHtmlEscaped(),
                 result.release_page.toHtmlEscaped()),
        dlg);
    title->setTextFormat(Qt::RichText);
    title->setOpenExternalLinks(true);
    head->addWidget(title, 1);
    lay->addLayout(head);

    const auto body = [&](const QString& text) {
        auto* l = new QLabel(text, dlg);
        l->setWordWrap(true);
        lay->addWidget(l);
        return l;
    };
    const auto note = [&](const QString& text) {
        auto* l = new QLabel(text, dlg);
        l->setWordWrap(true);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setStyleSheet(QStringLiteral("color:#9aa0a6; font-size:9pt;"));
        lay->addWidget(l);
        return l;
    };

    auto* buttons = new QHBoxLayout;
    auto* skip = new QPushButton(QStringLiteral("Skip This Version"), dlg);
    auto* later = new QPushButton(QStringLiteral("Later"), dlg);
    auto* primary = new QPushButton(dlg);
    primary->setDefault(true);
    buttons->addWidget(skip);
    buttons->addStretch(1);
    buttons->addWidget(later);
    buttons->addWidget(primary);

    const auto open_and_close = [dlg](const QString& url) {
        QDesktopServices::openUrl(QUrl(url));
        dlg->close();
    };
    const QString download = result.download_url;
    const QString page = result.release_page;
    const auto download_button = [&](const QString& label) {
        primary->setText(download.isEmpty() ? QStringLiteral("Open Release Page") : label);
        QObject::connect(primary, &QPushButton::clicked, dlg,
                         [open_and_close, download, page] { open_and_close(download.isEmpty() ? page : download); });
    };

    switch (channel) {
    case update::Channel::Flatpak: {
        body(QStringLiteral("Update Musa CAD from your software center, or run this command in a terminal:"));
        const QString cmd = QStringLiteral("flatpak update %1").arg(kFlathubAppId);
        auto* row = new QHBoxLayout;
        auto* field = new QLineEdit(cmd, dlg);
        field->setReadOnly(true);
        field->setStyleSheet(QStringLiteral("font-family: monospace;"));
        row->addWidget(field, 1);
        lay->addLayout(row);
        note(QStringLiteral("Installed from a downloaded .flatpak file? Switch to Flathub once, and updates "
                            "arrive with the rest of your apps:\nflatpak install --reinstall flathub %1")
                 .arg(kFlathubAppId));
        primary->setText(QStringLiteral("Copy Command"));
        QObject::connect(primary, &QPushButton::clicked, dlg, [primary, cmd] {
            QGuiApplication::clipboard()->setText(cmd);
            primary->setText(QStringLiteral("Copied"));
        });
        break;
    }
    case update::Channel::AppImage: {
        body(QStringLiteral("Download the new AppImage and use it in place of this one:"));
        note(QDir::toNativeSeparators(qEnvironmentVariable("APPIMAGE")));
        download_button(QStringLiteral("Download AppImage"));
        break;
    }
    case update::Channel::WindowsInstaller: {
        if (download.isEmpty() || !UpdateInstaller::supported()) {
            body(QStringLiteral("Download the new installer and run it. It updates this installation and keeps "
                                "your settings; save your drawings and close Musa CAD before it starts."));
            download_button(QStringLiteral("Download Installer"));
            break;
        }
        // The whole update from here: the installer is downloaded and checked, Musa CAD
        // saves and closes, the installer runs silently over this installation (Windows
        // asks once for administrator consent) and starts the new version.
        body(QStringLiteral("Musa CAD downloads the installer, checks it, and updates this installation in "
                            "place -- your settings and drawings stay as they are. You will be asked to save "
                            "any unsaved drawing, and Windows will ask for permission to install. The new "
                            "version starts when the update is done."));
        note(QStringLiteral("%1\nDownloads %2")
                 .arg(QDir::toNativeSeparators(UpdateInstaller::install_dir()),
                      UpdateInstaller::self_is_signed()
                          ? QStringLiteral("are accepted only with a valid signature.")
                          : QStringLiteral("are checked against the release's size, checksum and version.")));
        auto* status = new QLabel(dlg);
        status->setWordWrap(true);
        status->hide();
        lay->addWidget(status);
        auto* bar = new QProgressBar(dlg);
        bar->setRange(0, 0);
        bar->setTextVisible(true);
        bar->hide();
        lay->addWidget(bar);
        auto* installer = new UpdateInstaller(dlg);
        primary->setText(QStringLiteral("Download and Install"));
        QObject::connect(installer, &UpdateInstaller::stage, dlg, [status](const QString& what) {
            status->setText(what);
            status->show();
        });
        QObject::connect(installer, &UpdateInstaller::progress, dlg, [bar](qint64 done, qint64 total) {
            if (total > 0) {
                bar->setRange(0, 1000);
                bar->setValue(static_cast<int>(done * 1000 / total));
                bar->setFormat(QStringLiteral("%1 of %2 MB").arg(done / 1'000'000).arg(total / 1'000'000));
            }
            bar->show();
        });
        QObject::connect(installer, &UpdateInstaller::finished, dlg,
                         [dlg, status, bar, primary, skip, prepare_for_install](bool ok, const QString& what) {
                             bar->hide();
                             if (!ok) {
                                 status->setText(what);
                                 primary->setText(QStringLiteral("Try Again"));
                                 primary->setEnabled(true);
                                 skip->setEnabled(true);
                                 return;
                             }
                             status->setText(QStringLiteral("Checked. Saving your work and starting the update\u2026"));
                             // Unsaved drawings first; a Cancel there keeps this version running.
                             if (prepare_for_install && !prepare_for_install()) {
                                 status->setText(QStringLiteral("The update was postponed. The installer is ready at "
                                                                "%1; click Install to continue.")
                                                     .arg(QDir::toNativeSeparators(what)));
                                 primary->setText(QStringLiteral("Install"));
                                 primary->setEnabled(true);
                                 return;
                             }
                             QString err;
                             if (!UpdateInstaller::launch(what, UpdateInstaller::install_dir(), err)) {
                                 status->setText(err);
                                 primary->setText(QStringLiteral("Install"));
                                 primary->setEnabled(true);
                                 skip->setEnabled(true);
                                 return;
                             }
                             // The installer is waiting for this program to exit.
                             dlg->close();
                             QTimer::singleShot(0, qApp, &QCoreApplication::quit);
                         });
        QObject::connect(primary, &QPushButton::clicked, dlg, [installer, result, primary, skip, status] {
            primary->setEnabled(false);
            skip->setEnabled(false);
            status->setText(QStringLiteral("Starting\u2026"));
            status->show();
            installer->start(result);
        });
        // "Later" stays available while the download runs: it cancels it (and closes).
        QObject::connect(later, &QPushButton::clicked, installer, &UpdateInstaller::cancel);
        break;
    }
    case update::Channel::MacDmg:
        body(QStringLiteral("Download the new disk image, then drag Musa CAD into Applications to replace "
                            "this version."));
        download_button(QStringLiteral("Download"));
        break;
    case update::Channel::SourceBuild:
        body(QStringLiteral("This copy of Musa CAD was built from source. Pull the new version and rebuild, "
                            "or install a release package."));
        primary->setText(QStringLiteral("Open Release Page"));
        QObject::connect(primary, &QPushButton::clicked, dlg, [open_and_close, page] { open_and_close(page); });
        break;
    }

    lay->addSpacing(4);
    lay->addLayout(buttons);
    QObject::connect(later, &QPushButton::clicked, dlg, &QDialog::close);
    QObject::connect(skip, &QPushButton::clicked, dlg, [dlg, on_skip = std::move(on_skip)] {
        if (on_skip) {
            on_skip();
        }
        dlg->close();
    });
    dlg->setMinimumWidth(460);
    dlg->show();
    dlg->raise();
}

} // namespace musacad::ui
