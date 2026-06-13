// Headless plot verification tool. Renders through the REAL pipeline (parse -> store ->
// build_render_snapshot -> paint_plot) so a PDF can be checked without the GUI.
//   plot_check <tolerance> <out.pdf>                 -- synthetic circles (tessellation check)
//   plot_check --file <in.musa|in.dxf> <out.pdf> [aminx,aminy,amaxx,amaxy]
//        load a real drawing; report entity/insert/spline counts; plot its extents (or the
//        explicit window). For .dxf, exercises the DWG-import path (SPLINE/ELLIPSE included).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <QGuiApplication>
#include <QPageSize>
#include <QPdfWriter>
#include <QSizeF>

#include "musacad/core/block_resolve.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/scene_snapshot.hpp"
#include "musacad/ui/plot.hpp"

using namespace musacad;

namespace {
void plot_store(const core::GeometryStore& store, core::Vec2 amin, core::Vec2 amax,
                const std::string& out) {
    core::NativeKernel2D kernel;
    core::RenderSnapshot snap;
    // Tolerance: plotted region at paper-pixel resolution (same rule as MainWindow::prepare_plot).
    const double area_diag = std::max(core::length(amax - amin), 1e-9);
    const double paper_diag_px = std::hypot(297.0, 210.0) / 25.4 * 300.0;
    const double tol = std::max(area_diag / paper_diag_px * 0.3, 1e-9);
    core::build_render_snapshot(store, kernel, snap, tol, store.ltscale());

    ui::PlotSpec spec;
    spec.fit = true;
    spec.center = true;
    QPdfWriter w(QString::fromStdString(out));
    w.setPageSize(QPageSize(QSizeF(spec.paper_w_mm, spec.paper_h_mm), QPageSize::Millimeter));
    w.setResolution(300);
    ui::paint_plot(w, snap, spec, amin, amax);
    std::printf("[plot_check] lines=%zu fills=%zu tol=%.4f bounds=(%.1f,%.1f)..(%.1f,%.1f) -> %s\n",
                snap.line_vertices.size(), snap.fill_vertices.size(), tol, amin.x, amin.y, amax.x,
                amax.y, out.c_str());
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    if (argc > 2 && std::strcmp(argv[1], "--file") == 0) {
        const std::string path = argv[2];
        const std::string out = argc > 3 ? argv[3] : "/tmp/plot_check.pdf";
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        core::io::Document doc;
        const bool is_dxf = path.size() > 4 && path.substr(path.size() - 4) == ".dxf";
        const core::io::IoResult r =
            is_dxf ? core::io::parse_dxf(text, doc) : core::io::parse_native(text, doc);
        if (!r.ok) {
            std::printf("[plot_check] parse FAILED: %s\n", r.message.c_str());
            return 1;
        }
        std::printf("[plot_check] (%s) %s\n", is_dxf ? "DXF" : "native", r.message.c_str());
        core::GeometryStore store;
        core::io::populate_store(store, doc);

        // Insert resolution: how many block instances actually yield geometry.
        const auto& arena = store.inserts();
        std::size_t iseg = 0;
        int live = 0;
        std::vector<core::InsertSeg> segs;
        for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
            if (!arena.alive(i)) {
                continue;
            }
            ++live;
            segs.clear();
            core::resolve_insert(store, arena.data()[i], 0.1, segs);
            iseg += segs.size();
        }
        std::printf("[plot_check] inserts=%d -> %zu segments; lines=%zu polylines=%zu blocks=%zu\n",
                    live, iseg, doc.lines.size(), doc.polylines.size(), doc.block_defs.size());

        // Extents from a probe snapshot, unless an explicit window is given.
        core::NativeKernel2D kernel;
        core::RenderSnapshot probe;
        core::build_render_snapshot(store, kernel, probe, 0.5, store.ltscale());
        core::Vec2 mn{1e300, 1e300};
        core::Vec2 mx{-1e300, -1e300};
        for (const core::Vec2& v : probe.line_vertices) {
            mn = {std::min(mn.x, v.x), std::min(mn.y, v.y)};
            mx = {std::max(mx.x, v.x), std::max(mx.y, v.y)};
        }
        for (const core::Vec2& v : probe.fill_vertices) {
            mn = {std::min(mn.x, v.x), std::min(mn.y, v.y)};
            mx = {std::max(mx.x, v.x), std::max(mx.y, v.y)};
        }
        if (argc > 4) {
            double b[4] = {0, 0, 0, 0};
            std::sscanf(argv[4], "%lf,%lf,%lf,%lf", &b[0], &b[1], &b[2], &b[3]);
            mn = {b[0], b[1]};
            mx = {b[2], b[3]};
        }
        plot_store(store, mn, mx, out);
        return 0;
    }

    // Synthetic: two concentric circles + a line -> a tessellation/scale sanity check.
    const double tol = argc > 1 ? std::atof(argv[1]) : 0.02;
    const std::string out = argc > 2 ? argv[2] : "/tmp/plot_check.pdf";
    core::GeometryStore store;
    store.add_circle({0.0, 0.0}, 95.0);
    store.add_circle({0.0, 0.0}, 80.0);
    store.add_line({-120.0, -120.0}, {120.0, -120.0});
    core::NativeKernel2D kernel;
    core::RenderSnapshot snap;
    core::build_render_snapshot(store, kernel, snap, tol, 1.0);
    ui::PlotSpec spec;
    spec.fit = true;
    spec.center = true;
    QPdfWriter w(QString::fromStdString(out));
    w.setPageSize(QPageSize(QSizeF(spec.paper_w_mm, spec.paper_h_mm), QPageSize::Millimeter));
    w.setResolution(300);
    ui::paint_plot(w, snap, spec, {-130.0, -130.0}, {130.0, 130.0});
    std::printf("[plot_check] tol=%.4f line_vertices=%zu -> %s\n", tol, snap.line_vertices.size(),
                out.c_str());
    return 0;
}
