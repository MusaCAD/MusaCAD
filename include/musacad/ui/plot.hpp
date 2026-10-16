// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <memory>
#include <string_view>
#include <vector>

#include "musacad/core/math/vec2.hpp"
#include "musacad/core/page_setup.hpp"
#include "musacad/core/properties.hpp"

class QPaintDevice;
class QPainter;
class QPdfWriter;

namespace musacad::core {
struct RenderSnapshot;
class GeometryStore;
class IImageDecoder;
}

namespace musacad::ui {

class ImageSource;

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
[[nodiscard]] /// `computed_color`: the batch colour was computed (a gradient band) and is kept as is,
/// exempt from the near-white-plots-black screen convention.
core::Rgb plot_color(core::Rgb resolved, PlotSpec::Style style, bool computed_color = false);

/// THE shared plot renderer: paint `snap`'s geometry onto `device` for the world rectangle
/// [amin, amax], applying the world->paper transform (scale fit/ratio, centring/offset,
/// y-flip) and CTB style. Vector output (QPainter line/polygon operators). Both the PDF
/// (QPdfWriter) and printer (QPrinter) targets call this -- only the device differs. The
/// caller configures the device's page size/orientation/resolution first.
void paint_plot(QPaintDevice& device, const core::RenderSnapshot& snap, const PlotSpec& spec,
                core::Vec2 amin, core::Vec2 amax, const ImageSource* images = nullptr);
/// The same on a painter already open on `device` (one page of a PdfBook); the painter's
/// state is as it was afterwards.
void paint_plot(QPainter& painter, const QPaintDevice& device, const core::RenderSnapshot& snap, const PlotSpec& spec,
                core::Vec2 amin, core::Vec2 amax, const ImageSource* images = nullptr);

/// PUBLISH's PDF: one page per sheet, each page its own paper size. Paint each page while
/// its plot snapshot is current -- the next one replaces it -- then finish().
class PdfBook {
public:
    explicit PdfBook(std::string path);
    ~PdfBook();
    PdfBook(const PdfBook&) = delete;
    PdfBook& operator=(const PdfBook&) = delete;
    void add_page(const core::RenderSnapshot& snap, const PlotSpec& spec, core::Vec2 amin, core::Vec2 amax,
                  const ImageSource* images = nullptr);
    /// Closes the file; false (with `error`) when nothing was written.
    [[nodiscard]] bool finish(std::string& error);
    [[nodiscard]] int pages() const noexcept { return pages_; }

private:
    std::string path_;
    std::unique_ptr<QPdfWriter> writer_;
    std::unique_ptr<QPainter> painter_;
    int pages_ = 0;
};

/// Supplies decoded pixels for the snapshot's ImageInstances at paint time. The snapshot
/// deliberately carries only transforms (it is copied through the triple buffer on every
/// publish), so whoever paints it provides the raster. Returning false simply omits that
/// image -- a missing external file must not abort a plot of the rest of the drawing.
class ImageSource {
public:
    virtual ~ImageSource() = default;
    /// Fill `rgba` (width*height*4, top row first) for image definition `def`.
    [[nodiscard]] virtual bool pixels(std::uint16_t def, std::uint32_t& width,
                                      std::uint32_t& height,
                                      std::vector<std::uint8_t>& rgba) const = 0;
};

/// Convert between the UI PlotSpec and the persisted core::PageSetup (one model, two
/// faces). The copies field is UI-only (not saved) and the printer target is kept as-is.
[[nodiscard]] core::PageSetup to_page_setup(const PlotSpec& s, const std::string& name);
[[nodiscard]] PlotSpec from_page_setup(const core::PageSetup& ps);

// ---------------------------------------------------------------------------
// The plot path's shared pieces. Everything here has BOTH a GUI caller (the PLOT
// dialog) and a headless caller (the CLI / plot_check), so there is exactly one
// definition of the paper table, the tessellation rule, and the PDF device setup.
// ---------------------------------------------------------------------------

/// A standard sheet, as (long edge, short edge) millimetres.
struct PaperSize {
    const char* name;
    double long_mm;
    double short_mm;
};

/// The standard sheets Musa CAD offers, in the order the PLOT dialog lists them.
[[nodiscard]] std::span<const PaperSize> standard_papers();

/// Resolve a paper name to the sheet dimensions IN the requested orientation.
/// Matching is case-insensitive and tolerant of the short form, so "A4", "a4",
/// "ISO A4" and "Letter" all resolve. Returns false for an unknown name.
[[nodiscard]] bool resolve_paper(std::string_view name, bool landscape, double& w_mm,
                                 double& h_mm);

/// The curve-tessellation tolerance for a plot: ~0.3 px chord deviation at 300 DPI
/// across the PLOTTED region (not the whole drawing). Tying it to the plotted area is
/// what keeps a circle that fills a picked window smooth while staying cheap for a
/// large sheet. THE single definition -- the GUI, the CLI and plot_check all call it.
[[nodiscard]] double plot_tolerance(core::Vec2 amin, core::Vec2 amax, double paper_w_mm,
                                    double paper_h_mm);

/// Write `snap` to `path` as a vector PDF under `spec`. Configures the QPdfWriter
/// (page size in the spec's orientation, 300 DPI) and paints through `paint_plot` --
/// the same renderer the GUI and the printer target use; only the device differs.
/// Returns false with a reason in `error` if the file could not be written.
[[nodiscard]] bool write_plot_pdf(const std::string& path, const core::RenderSnapshot& snap,
                                  const PlotSpec& spec, core::Vec2 amin, core::Vec2 amax,
                                  std::string& error, const ImageSource* images = nullptr);

/// An ImageSource backed by the store's image-definition table and an IImageDecoder.
/// Decodes on first use and caches by definition index, so plotting N placements of one
/// logo decodes once. `drawing_dir` resolves external sources -- see resolve_image_path,
/// which refuses anything outside that directory.
[[nodiscard]] std::unique_ptr<ImageSource> make_store_image_source(
    const core::GeometryStore& store, const core::IImageDecoder* decoder,
    std::string_view drawing_dir);

} // namespace musacad::ui
