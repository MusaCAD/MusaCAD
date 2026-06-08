#include "musacad/core/text/mtext.hpp"

#include <cmath>
#include <string>

#include "musacad/core/text/stroke_font.hpp"

namespace musacad::core::text {

namespace {

// Greedy word-wrap: split `content` on '\n', then wrap each paragraph so each
// line's scaled width fits `max_w` (<= 0 means no wrap). A single over-long word
// gets its own line (no mid-word splitting -- a documented simplification).
std::vector<std::string> wrap_lines(std::string_view content, double max_w, double height,
                                    double width_factor) {
    std::vector<std::string> lines;
    const auto scaled = [&](const std::string& s) { return text_width(s, height) * width_factor; };
    std::size_t start = 0;
    while (start <= content.size()) {
        std::size_t nl = content.find('\n', start);
        const std::string_view para =
            content.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        if (max_w <= 0.0) {
            lines.emplace_back(para);
        } else {
            std::string line;
            std::size_t i = 0;
            while (i < para.size()) {
                // next word + following spaces
                std::size_t ws = para.find(' ', i);
                const std::string word(para.substr(i, ws == std::string_view::npos ? ws : ws - i));
                const std::string candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() && scaled(candidate) > max_w) {
                    lines.push_back(line);
                    line = word;
                } else {
                    line = candidate;
                }
                i = (ws == std::string_view::npos) ? para.size() : ws + 1;
            }
            lines.push_back(line); // may be empty (blank paragraph)
        }
        if (nl == std::string_view::npos) {
            break;
        }
        start = nl + 1;
    }
    return lines;
}

} // namespace

MTextLayout layout_mtext(const MTextBlock& block, std::string_view content) {
    MTextLayout out;
    const double height = block.height > 0.0 ? block.height : 1.0;
    const double wf = block.width_factor > 0.0 ? block.width_factor : 1.0;
    const double line_height = height * (block.line_spacing > 0.0 ? block.line_spacing : 1.0);
    const std::vector<std::string> lines = wrap_lines(content, block.width, height, wf);
    const int n = static_cast<int>(lines.size());
    out.line_count = n;

    double maxw = 0.0;
    for (const std::string& l : lines) {
        maxw = std::max(maxw, text_width(l, height) * wf);
    }
    // The block's wrap width governs the box when set, else the longest line.
    const double box_w = block.width > 0.0 ? block.width : maxw;
    const double total_h = static_cast<double>(std::max(n, 1)) * line_height;

    // attach: col = L/C/R (0/1/2), row = T/M/B (0/1/2). The attachment point `pos`
    // maps to (ax, ay) in the local box (left=0,right=box_w; top=0,bottom=-total_h).
    const int col = block.attach % 3;
    const int row = block.attach / 3;
    const double ax = (col == 1) ? box_w * 0.5 : (col == 2 ? box_w : 0.0);
    const double ay = (row == 1) ? -total_h * 0.5 : (row == 2 ? -total_h : 0.0);

    const double cs = std::cos(block.rotation);
    const double sn = std::sin(block.rotation);
    const auto to_world = [&](double lx, double ly) -> Vec2 {
        const double x = lx - ax;
        const double y = ly - ay;
        return {block.pos.x + x * cs - y * sn, block.pos.y + x * sn + y * cs};
    };

    bool first = true;
    const auto extend = [&](Vec2 w) {
        if (first) {
            out.min = out.max = w;
            first = false;
        } else {
            out.min = {std::min(out.min.x, w.x), std::min(out.min.y, w.y)};
            out.max = {std::max(out.max.x, w.x), std::max(out.max.y, w.y)};
        }
    };
    // The defined box corners anchor the AABB (stable even with short lines).
    extend(to_world(0.0, 0.0));
    extend(to_world(box_w, 0.0));
    extend(to_world(0.0, -total_h));
    extend(to_world(box_w, -total_h));

    std::vector<Vec2> glyphs;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const double linew = text_width(lines[i], height) * wf;
        const double line_x = (col == 1) ? (box_w - linew) * 0.5 : (col == 2 ? box_w - linew : 0.0);
        const double baseline = -static_cast<double>(i) * line_height - height;
        glyphs.clear();
        append_text_segments(lines[i], {0.0, 0.0}, height, 0.0, Justify::Left, glyphs);
        for (const Vec2& g : glyphs) {
            const Vec2 w = to_world(line_x + g.x * wf, baseline + g.y);
            out.segments.push_back(w);
            extend(w);
        }
    }
    if (first) { // no content/box: degenerate
        out.min = out.max = block.pos;
    }
    return out;
}

} // namespace musacad::core::text
