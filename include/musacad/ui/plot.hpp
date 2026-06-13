#pragma once

#include <cstdint>
#include <string>

#include "musacad/core/math/vec2.hpp"
#include "musacad/core/page_setup.hpp"
#include "musacad/core/properties.hpp"

class QPaintDevice;

namespace musacad::core {
struct RenderSnapshot;
}

namespace musacad::ui {

/// A plot configuration (the PLOT dialog's fields). Independent of any Qt device so it
/// can be persisted (as a core PageSetup) and reused for both PDF and printer targets.
struct PlotSpec {
    enum class Area : std::uint8_t { Display, Extents, Window };
    enum class Style : std::uint8_t { None, Monochrome, Grayscale }; // built-in CTB plot styles

    // Paper (millimetres), already in the chosen orientation when applied to the device.
    double paper_w_mm = 297.0;
    double paper_h_mm = 210.0;
    std::string paper = "ISO A4";
    bool landscape = true;

    Area area = Area::Extents;
    core::Vec2 win_min{}; // world rect for Area::Window
    core::Vec2 win_max{};

    bool fit = true;             // fit the area to the paper
    double scale_num = 1.0;      // plotted mm ...
    double scale_den = 1.0;      // ... per this many drawing units (used when !fit)

    bool center = true;
    double off_x_mm = 0.0;
    double off_y_mm = 0.0;

    bool plot_lineweights = true; // plot object lineweights (else hairline)
    Style style = Style::None;    // CTB plot style
    int copies = 1;               // printer only
    std::string target = "PDF";   // "PDF" or a QPrinterInfo printer name
};

/// CTB as a plot-time resolution LAYER over the Ph12-resolved batch colour (one model,
/// not a fork). Always maps near-white to black so white-on-dark-screen geometry is
/// visible on white paper (the universal CAD rule). Then: None = as-is; Monochrome =
/// black; Grayscale = the colour's luminance.
[[nodiscard]] core::Rgb plot_color(core::Rgb resolved, PlotSpec::Style style);

/// THE shared plot renderer: paint `snap`'s geometry onto `device` for the world rectangle
/// [amin, amax], applying the world->paper transform (scale fit/ratio, centring/offset,
/// y-flip) and CTB style. Vector output (QPainter line/polygon operators). Both the PDF
/// (QPdfWriter) and printer (QPrinter) targets call this -- only the device differs. The
/// caller configures the device's page size/orientation/resolution first.
void paint_plot(QPaintDevice& device, const core::RenderSnapshot& snap, const PlotSpec& spec,
                core::Vec2 amin, core::Vec2 amax);

/// Convert between the UI PlotSpec and the persisted core::PageSetup (one model, two
/// faces). The copies field is UI-only (not saved) and the printer target is kept as-is.
[[nodiscard]] core::PageSetup to_page_setup(const PlotSpec& s, const std::string& name);
[[nodiscard]] PlotSpec from_page_setup(const core::PageSetup& ps);

} // namespace musacad::ui
