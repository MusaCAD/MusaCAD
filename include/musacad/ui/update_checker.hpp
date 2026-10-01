// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <functional>

#include <QObject>
#include <QString>

#include "musacad/ui/update_channel.hpp"

class QNetworkAccessManager;
class QWidget;

namespace musacad::ui {

/// Asks the channel this copy was installed from for the latest version, off the UI
/// thread (one asynchronous request): Flathub for the Flatpak -- what `flatpak update`
/// will actually deliver -- and the GitHub release for every other package. The answer
/// arrives as finished(); nothing here shows UI.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool ok = false;
        QString error;        ///< why the check failed (ok == false)
        QString latest;       ///< the newest published version, e.g. "0.6.0"
        QString release_page; ///< the release notes on GitHub
        QString download_url; ///< the file that updates this channel; "" when none
        QString download_name; ///< that file's name, e.g. MusaCAD-0.6.0-x86_64-setup.exe
        qint64 download_size = 0; ///< its size as the release lists it; 0 when unknown
        QString checksum_url; ///< the SHA-256 the release publishes beside it; "" when none
    };

    explicit UpdateChecker(QObject* parent = nullptr);

    /// How this copy was installed (Flatpak, AppImage, the Windows installer, a macOS
    /// app bundle, or a source build).
    [[nodiscard]] static update::Channel detect_channel();
    /// The running version, e.g. "0.5.0".
    [[nodiscard]] static QString current_version();

    /// Start one check; a check already in flight makes this a no-op.
    void check();
    [[nodiscard]] bool busy() const noexcept { return busy_; }

Q_SIGNALS:
    void finished(const musacad::ui::UpdateChecker::Result& result);

private:
    void finish(Result result);

    QNetworkAccessManager* net_ = nullptr;
    bool busy_ = false;
};

/// The "update available" window: what is new, and the one step that updates this copy
/// (the Flathub command to copy, the file to download, or -- for the Windows installer --
/// the download-check-install run done from here). Non-modal, so the drawing stays usable
/// behind it. `on_skip` runs when the user skips this version; `prepare_for_install` runs
/// right before an in-place update replaces the program (save the open drawings; false
/// keeps the current version running).
void show_update_dialog(QWidget* parent, const UpdateChecker::Result& result, update::Channel channel,
                        std::function<void()> on_skip, std::function<bool()> prepare_for_install = {});

} // namespace musacad::ui
