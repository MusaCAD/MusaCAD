#pragma once

#include <QString>

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

    /// Detects a converter. Order: (1) the configured path in QSettings
    /// `io/dwg_converter_path` (kind inferred from its basename), (2) ODA File
    /// Converter on PATH, (3) LibreDWG `dwg2dxf` on PATH. Returns a None converter
    /// if nothing is found (callers degrade gracefully -- see install_hint()).
    [[nodiscard]] static DwgConverter discover();

    /// PATH-only discovery (ignores the configured setting): ODA then LibreDWG.
    [[nodiscard]] static DwgConverter discover_on_path();

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

private:
    Kind kind_ = Kind::None;
    QString program_;
};

} // namespace musacad::ui
