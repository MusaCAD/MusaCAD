// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <QString>
#include <QStringList>

#include <utility>

namespace musacad::ui {

/// DWG <-> DXF conversion via an EXTERNAL converter program that the USER installs.
///
/// LICENSING: Musa CAD is LGPL. The DWG converters (LibreDWG, ODA File Converter)
/// are GPL / proprietary and are NEVER linked, bundled, vendored, or added to the
/// build. Musa CAD only INVOKES one as a subprocess and reads the DXF it writes --
/// the licensing boundary is a process boundary. The converter is discovered at
/// runtime on the user's machine; it is not shipped with Musa CAD.
class DwgConverter {
public:
    /// Which converter was found (governs the command line we build).
    enum class Kind {
        None,      ///< no converter available
        Generic,   ///< a user-configured wrapper invoked as `<prog> <in> <out>`
        LibreDwg,  ///< LibreDWG dwg2dxf / dxf2dwg
        Oda,       ///< ODA File Converter (directory-batch protocol)
    };

    /// True inside a Flatpak sandbox (`/.flatpak-info` exists, or FLATPAK_ID is set).
    /// There the converter is not on the sandbox's PATH: DWG stays off unless the
    /// user opts in to a converter installed on the HOST (see host_mode()).
    [[nodiscard]] static bool in_flatpak();
    /// The opt-in (QSettings `io/dwg_host_converter`): inside the sandbox, look the
    /// converter up and run it on the host through `flatpak-spawn --host`. Needs the
    /// one-time `flatpak override --user --talk-name=org.freedesktop.Flatpak
    /// org.musacad.MusaCAD` the DWG Setup dialog spells out; the default stays sandboxed.
    [[nodiscard]] static bool host_mode();
    static void set_host_mode(bool on);
    /// The command line that runs `program args...` on the host: flatpak-spawn --host
    /// program args... (pure; what run_sync uses in host mode).
    [[nodiscard]] static std::pair<QString, QStringList> host_command(const QString& program,
                                                                     const QStringList& args);
    /// True when this converter runs on the host (found or configured in host mode).
    [[nodiscard]] bool on_host() const noexcept { return host_; }

    /// Detects a converter. Order: (1) the configured path in QSettings
    /// `io/dwg_converter_path` (kind inferred from its basename), (2) the converter
    /// Musa CAD downloaded (discover_managed()), (3) ODA File Converter on PATH,
    /// (4) LibreDWG `dwg2dxf` on PATH. Returns a None converter
    /// if nothing is found (callers degrade gracefully -- see install_hint()). In host
    /// mode the lookups and the existence checks happen on the host.
    [[nodiscard]] static DwgConverter discover();

    /// PATH-only discovery (ignores the configured setting): ODA then LibreDWG.
    [[nodiscard]] static DwgConverter discover_on_path();

    /// The converter Musa CAD downloaded itself (OdaInstaller), if one is installed:
    /// ODA File Converter under the app's data directory. Inside the Flatpak it runs
    /// on the host when the sandbox has no X11 display (the converter needs one) or
    /// host mode is on.
    [[nodiscard]] static DwgConverter discover_managed();
    /// What a blank setting resolves to: the downloaded converter, else PATH.
    [[nodiscard]] static DwgConverter discover_default();

    /// Build a converter from an explicit program path (kind inferred from the
    /// basename). Available only if the file exists. For the setup dialog's "Browse".
    [[nodiscard]] static DwgConverter from_program(const QString& path);

    /// Human-readable name of the detected kind (for the setup dialog status line).
    [[nodiscard]] static QString kind_name(Kind k);

    /// A clear, actionable message telling the user what to install / configure when
    /// no converter is available. Shown verbatim; never a crash or silent failure.
    [[nodiscard]] static QString install_hint();

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool available() const noexcept { return kind_ != Kind::None; }
    [[nodiscard]] const QString& program() const noexcept { return program_; }

    /// Convert a .dwg to a .dxf at `dxf_out` (synchronous; blocks the caller -- run
    /// it off the UI thread). Returns false and fills `err` on any failure (missing
    /// program, non-zero exit, timeout, or no output produced). Never throws.
    [[nodiscard]] bool to_dxf(const QString& dwg_in, const QString& dxf_out, QString& err) const;

    /// Convert a .dxf to a .dwg at `dwg_out`, targeting `version` (e.g. "ACAD2018").
    /// Synchronous; same failure contract as to_dxf().
    [[nodiscard]] bool to_dwg(const QString& dxf_in, const QString& dwg_out, const QString& version,
                              QString& err) const;

    // Construction is via discover(); this overload is for tests (inject a kind+prog).
    DwgConverter() = default;
    DwgConverter(Kind kind, QString program) : kind_(kind), program_(std::move(program)) {}
    DwgConverter(Kind kind, QString program, bool host) : kind_(kind), program_(std::move(program)), host_(host) {}

private:
    Kind kind_ = Kind::None;
    QString program_;
    bool host_ = false; ///< runs on the host through flatpak-spawn (Flatpak opt-in)
};

} // namespace musacad::ui
