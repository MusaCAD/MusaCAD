// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <QObject>
#include <QString>

#include "musacad/ui/update_checker.hpp"

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace musacad::ui {

/// Applies an update to the Windows installer package from inside the running program:
/// downloads the new setup from the GitHub release, checks it, and runs it silently over
/// this installation (the installer waits for Musa CAD to exit, updates in place and
/// starts the new version as the user).
///
/// What is trusted, and how far. The address comes from the release the update check
/// read over TLS from api.github.com, and only https to github.com and its file hosts is
/// followed. The download must then be the file the release published: its size, the
/// SHA-256 the release carries beside it (when it does), and a Windows executable whose
/// version resource says it is Musa CAD of that version. When this program is itself
/// code-signed, the new installer must carry a valid Authenticode signature too --
/// unsigned builds skip that step, and say so. Nothing is run before all of that holds.
///
/// On other platforms every method reports "not supported"; the checker's dialog never
/// offers them there.
class UpdateInstaller : public QObject {
    Q_OBJECT
public:
    explicit UpdateInstaller(QObject* parent = nullptr);
    ~UpdateInstaller() override;

    /// Whether this build can apply an update to itself (Windows, installed by the
    /// setup program).
    [[nodiscard]] static bool supported();
    /// Where downloads land: <app data>/updates.
    [[nodiscard]] static QString downloads_dir();
    /// The folder this installation lives in (what the installer is told to update).
    [[nodiscard]] static QString install_dir();
    /// Whether the running program carries a valid Authenticode signature (then the
    /// downloaded installer must too).
    [[nodiscard]] static bool self_is_signed();

    /// The checks a downloaded installer must pass before it is run; false with `err`
    /// naming the first one that failed. `expected_sha256` may be "" (no checksum
    /// published); `expected_size` 0 means unknown.
    [[nodiscard]] static bool verify_installer(const QString& path, const QString& version,
                                               qint64 expected_size, const QString& expected_sha256,
                                               bool require_signature, QString& err);

    /// Run a verified installer over `install_dir` (silent, elevated: Windows asks for
    /// consent). Returns once the installer process exists; the caller then quits so the
    /// installer can replace the files. False with `err` when the user declined or the
    /// program could not start.
    [[nodiscard]] static bool launch(const QString& installer, const QString& install_dir, QString& err);

    /// Remove downloads of versions no newer than the running one (leftovers of an
    /// applied update, or of one that was declined and is now stale).
    static void remove_stale_downloads();

    /// Download the release's installer for this package to downloads_dir() and verify
    /// it. Signals: stage() as each step starts, progress() while downloading, and
    /// finished(ok, path-or-error) once.
    void start(const UpdateChecker::Result& update);
    void cancel();

Q_SIGNALS:
    void stage(const QString& what);
    void progress(qint64 done, qint64 total);
    void finished(bool ok, const QString& file_or_error);

private:
    void fetch_checksum();
    void fetch_installer();
    void verify_and_finish();
    void fail(const QString& why);
    QNetworkReply* get(const QString& url);

    QNetworkAccessManager* net_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QFile* out_ = nullptr;
    UpdateChecker::Result update_;
    QString target_;   ///< the installer's final path
    QString sha256_;   ///< from the published checksum file, "" when there is none
    bool cancelled_ = false;
    bool done_ = false;
};

} // namespace musacad::ui
