// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <optional>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace musacad::ui {

/// Fetches the ODA File Converter from the Open Design Alliance's own download server into
/// a directory Musa CAD manages, and points the DWG setting at it -- the one-click
/// alternative to finding the download page, installing the program and browsing to it.
///
/// Boundaries, unchanged from DwgConverter: the converter is a separate program under the
/// Open Design Alliance's terms. Musa CAD does not bundle or redistribute it; it downloads
/// it at the user's request, after they accept those terms, and runs it as a subprocess.
///
/// The download page names the current release (the file names carry the version), so
/// the installer reads it first and falls back to the release known at build time when the
/// page cannot be read. The file link redirects to a short-lived signed URL, hence a GET
/// with redirects followed, never a HEAD.
class OdaInstaller : public QObject {
    Q_OBJECT
public:
    struct Release {
        QString version;  ///< "27.1"
        QString filename; ///< the file for this platform, e.g. ODAFileConverter_QT6_lnxX64_8.3dll_27.1.AppImage
        qint64 size_hint = 0; ///< bytes, when known (the acknowledgement quotes it)
    };

    /// Where the converter is kept: <app data>/converters/oda (MUSACAD_ODA_DIR overrides it,
    /// for the self-test).
    [[nodiscard]] static QString install_dir();
    /// The installed program inside install_dir(), or "" when nothing is installed there.
    [[nodiscard]] static QString installed_program();
    /// Delete the managed install (and clear the DWG setting if it pointed there).
    static bool remove_installed(QString& err);

    /// The download page, and the file link for a named file.
    [[nodiscard]] static QUrl page_url();
    [[nodiscard]] static QUrl download_url(const QString& filename);
    /// A regular expression matching this platform's file name on the page (the version
    /// as group 1), or "" when the Open Design Alliance publishes no build for it.
    [[nodiscard]] static QString platform_pattern();
    /// The current release for this platform as the page names it, if it does.
    [[nodiscard]] static std::optional<Release> parse_release(const QByteArray& page_html);
    /// The release known at build time (used when the page cannot be read).
    [[nodiscard]] static Release fallback_release();

    /// Unpack a downloaded file into install_dir() and name the program. Linux: the
    /// AppImage unpacks itself (`--appimage-extract`; no FUSE, so it works in a sandbox
    /// too); Windows: an administrative MSI extraction (`msiexec /a`, no elevation);
    /// macOS: the disk image is attached and the app bundle copied out. Synchronous
    /// (run it off the GUI thread); false with `err` on any failure.
    static bool install_from_file(const QString& archive, QString& program, QString& err);

    explicit OdaInstaller(QObject* parent = nullptr);
    ~OdaInstaller() override;

    /// Read the page, then download this platform's file to install_dir()/download/.
    /// Signals: stage() as each step starts, progress() during the download, finished()
    /// once with the downloaded file's path (or the error). Nothing is unpacked here.
    void start();
    /// Abort a running download; finished(false, "Cancelled.") follows.
    void cancel();
    /// The release being fetched (known once the page was read or the fallback taken).
    [[nodiscard]] const Release& release() const noexcept { return release_; }

Q_SIGNALS:
    void stage(const QString& what);
    void progress(qint64 done, qint64 total);
    void finished(bool ok, const QString& file_or_error);

private:
    void fetch_page();
    void fetch_file();
    void fail(const QString& why);

    QNetworkAccessManager* net_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QFile* out_ = nullptr;
    Release release_;
    QString target_;
    bool cancelled_ = false;
    bool done_ = false;
};

} // namespace musacad::ui
