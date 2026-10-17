// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/text/mtext.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>

#include "musacad/core/font_engine.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/text/stroke_font.hpp"
#include "musacad/core/text/text_codes.hpp"

namespace musacad::core::text {

namespace {

// Greedy word-wrap: split `content` on '\n', then wrap each paragraph so each line's
// width (via `measure`) fits `max_w` (<= 0 means no wrap). A single over-long word gets
// its own line (no mid-word splitting -- a documented simplification). `measure` is the
// stroke- or outline-font width function, so wrapping matches the rendered glyphs.
std::vector<std::string> wrap_lines(std::string_view content, double max_w,
                                    const std::function<double(std::string_view)>& measure) {
    std::vector<std::string> lines;
    const auto scaled = [&](const std::string& s) { return measure(s); };
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

// --- MTEXT format codes ------------------------------------------------------------
// AutoCAD's inline codes: \P paragraph, \H height (`2.5;` or `0.8x;`), \W width factor,
// \T tracking, \Q obliquing, \C colour (ACI) and \c (true colour), \f / \F font, \L \O
// \K under-, over- and strike-through on (lower case: off), \S stacking (`1/2;` with a
// bar, `1#2;` diagonal, `+0.1^-0.1;` a tolerance), \~ a space no line breaks at, \\ \{
// \} literals, { } to group, ^I a tab. \A and \p (alignment, paragraph settings) are read
// and passed over.

constexpr char kNbsp = '\x1F'; // \~ inside a run: a space where no line breaks

struct RunStyle {
    double height = 1.0;
    double width = 1.0;    // \W, on top of the block's width factor
    double tracking = 1.0; // \T
    double oblique = 0.0;  // \Q, radians
    bool underline = false;
    bool overline = false;
    bool strike = false;
    bool colored = false;
    Rgb color{};
    std::string font; // "" = the block's
};

struct Item {
    enum class Kind : std::uint8_t { Text, Break, Stack };
    Kind kind = Kind::Text;
    RunStyle style;
    std::string text; // raw: the %% codes expand when drawn
    std::string top;
    std::string bottom;
    char stack = '/';
};

bool code_letter(char c) {
    return std::string_view("PNXLlOoKkHWTQCcFfApS~{}\\").find(c) != std::string_view::npos;
}

std::vector<Item> parse_codes(std::string_view raw, double base_height) {
    std::vector<Item> items;
    std::vector<RunStyle> styles{RunStyle{}};
    styles.back().height = base_height;
    std::string buf;
    const auto flush = [&] {
        if (!buf.empty()) {
            items.push_back(Item{Item::Kind::Text, styles.back(), buf, {}, {}, '/'});
            buf.clear();
        }
    };
    const auto brk = [&] {
        flush();
        items.push_back(Item{Item::Kind::Break, styles.back(), {}, {}, {}, '/'});
    };
    // The argument after a code letter at raw[i], up to its ';' (i left on the ';').
    const auto arg = [&](std::size_t& i) {
        std::string a;
        std::size_t j = i + 1;
        while (j < raw.size() && raw[j] != ';') {
            if (raw[j] == '\\' && j + 1 < raw.size()) {
                ++j; // an escaped character (\; \/ \#) is itself
            }
            a += raw[j++];
        }
        i = j;
        return a;
    };
    const auto number = [](const std::string& a, double fallback) {
        char* end = nullptr;
        const double v = std::strtod(a.c_str(), &end);
        return end == a.c_str() ? fallback : v;
    };
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '{') {
            flush();
            styles.push_back(styles.back());
            continue;
        }
        if (c == '}') {
            flush();
            if (styles.size() > 1) {
                styles.pop_back();
            }
            continue;
        }
        if (c == '\n') {
            brk();
            continue;
        }
        if (c == '^' && i + 1 < raw.size() && (raw[i + 1] == 'I' || raw[i + 1] == 'J' || raw[i + 1] == 'M')) {
            const char n = raw[++i];
            if (n == 'I') {
                buf += "    "; // a tab
            } else if (n == 'J') {
                brk();
            }
            continue;
        }
        if (c != '\\' || i + 1 >= raw.size()) {
            buf += c;
            continue;
        }
        const char n = raw[i + 1];
        if (n == 'U' || n == 'u') {
            buf += c; // \U+XXXX: expanded with the %% codes when drawn
            continue;
        }
        if (!code_letter(n)) {
            buf += n; // an unknown code: its letter, as AutoCAD shows it
            ++i;
            continue;
        }
        ++i; // on the letter
        RunStyle& s = styles.back();
        switch (n) {
        case 'P':
        case 'N':
        case 'X':
            brk();
            break;
        case '~':
            buf += kNbsp;
            break;
        case '\\':
        case '{':
        case '}':
            buf += n;
            break;
        case 'L':
        case 'l':
            flush();
            s.underline = n == 'L';
            break;
        case 'O':
        case 'o':
            flush();
            s.overline = n == 'O';
            break;
        case 'K':
        case 'k':
            flush();
            s.strike = n == 'K';
            break;
        case 'S': {
            flush();
            const std::string a = arg(i);
            std::size_t at = std::string::npos;
            for (std::size_t k = 0; k < a.size() && at == std::string::npos; ++k) {
                if (a[k] == '/' || a[k] == '#' || a[k] == '^') {
                    at = k;
                }
            }
            Item st{Item::Kind::Stack, s, {}, {}, {}, '/'};
            st.top = at == std::string::npos ? a : a.substr(0, at);
            st.bottom = at == std::string::npos ? std::string() : a.substr(at + 1);
            st.stack = at == std::string::npos ? '/' : a[at];
            items.push_back(std::move(st));
            break;
        }
        default: {
            flush();
            std::string a = arg(i);
            const bool rel = !a.empty() && (a.back() == 'x' || a.back() == 'X');
            if (rel) {
                a.pop_back();
            }
            if (n == 'H') {
                const double v = number(a, 0.0);
                if (v > 0.0) {
                    s.height = rel ? s.height * v : v;
                }
            } else if (n == 'W') {
                const double v = number(a, 0.0);
                if (v > 0.0) {
                    s.width = rel ? s.width * v : v;
                }
            } else if (n == 'T') {
                const double v = number(a, 0.0);
                if (v > 0.0) {
                    s.tracking = v;
                }
            } else if (n == 'Q') {
                s.oblique = std::clamp(number(a, 0.0), -85.0, 85.0) * 3.14159265358979323846 / 180.0;
            } else if (n == 'C') {
                const long aci = std::lround(number(a, 256.0));
                s.colored = aci > 0 && aci < 256;
                if (s.colored) {
                    s.color = io::dxf_aci_to_rgb(aci);
                }
            } else if (n == 'c') {
                const long v = std::lround(number(a, -1.0));
                s.colored = v >= 0;
                if (s.colored) {
                    s.color = Rgb{static_cast<std::uint8_t>((v >> 16) & 0xFF), static_cast<std::uint8_t>((v >> 8) & 0xFF),
                                  static_cast<std::uint8_t>(v & 0xFF)};
                }
            } else if (n == 'f' || n == 'F') {
                std::string name = a.substr(0, a.find('|'));
                const std::size_t dot = name.rfind('.');
                if (dot != std::string::npos) {
                    name.erase(dot); // romans.shx, arial.ttf: by its name
                }
                s.font = name;
            } // \A, \p: read and passed over
            break;
        }
        }
    }
    flush();
    return items;
}

} // namespace

// Formatted when it has a code with its argument and ';' (\H2.5; \C1; \fArial; \S1/2;) or
// a toggle turned on and off again (\L..\l): a backslash a user typed (C:\Program Files)
// is not taken for a code.
bool has_mtext_codes(std::string_view content) {
    const auto has = [&](std::string_view code) { return content.find(code) != std::string_view::npos; };
    for (std::size_t i = 0; i + 1 < content.size(); ++i) {
        if (content[i] == '\\' && std::string_view("HCcFfWTQApS").find(content[i + 1]) != std::string_view::npos &&
            content.find(';', i + 2) != std::string_view::npos) {
            return true;
        }
    }
    return (has("\\L") && has("\\l")) || (has("\\O") && has("\\o")) || (has("\\K") && has("\\k"));
}

std::string mtext_plain(std::string_view content, bool all_codes) {
    if (!all_codes && !has_mtext_codes(content)) {
        return std::string(content);
    }
    std::string out;
    for (const Item& it : parse_codes(content, 1.0)) {
        if (it.kind == Item::Kind::Break) {
            out += '\n';
        } else if (it.kind == Item::Kind::Stack) {
            out += it.top + (it.bottom.empty() ? "" : "/" + it.bottom);
        } else {
            for (const char ch : it.text) {
                out += ch == kNbsp ? ' ' : ch;
            }
        }
    }
    return out;
}

double text_advance(const IFontEngine* fonts, std::string_view font_name, std::string_view text,
                    double height) {
    // Measure the VISIBLE text -- control codes (%%c50 -> "(diameter)50") expand at render
    // time, so the width must match the rendered glyphs (storage stays raw).
    const std::string vis = substitute_text(text);
    if (fonts != nullptr && fonts->is_outline_font(font_name)) {
        return fonts->advance(font_name, vis, height);
    }
    return text_width(vis, height);
}

namespace {

// MTEXT with format codes: the text in runs of their own height, width, slant, colour and
// font, wrapped as words across the runs, each line as tall as its tallest run.
MTextLayout layout_formatted(const MTextBlock& block, std::string_view content, const IFontEngine* fonts,
                             std::string_view font_name) {
    MTextLayout out;
    const double height = block.height > 0.0 ? block.height : 1.0;
    const double wf = block.width_factor > 0.0 ? block.width_factor : 1.0;
    const std::vector<Item> items = parse_codes(content, height);
    const auto font_of = [&](const RunStyle& s) { return s.font.empty() ? font_name : std::string_view(s.font); };
    const auto outline_of = [&](const RunStyle& s) { return fonts != nullptr && fonts->is_outline_font(font_of(s)); };
    const auto visible = [](std::string_view t) {
        std::string v = substitute_text(t);
        std::replace(v.begin(), v.end(), kNbsp, ' ');
        return v;
    };
    const auto measure = [&](const RunStyle& s, double h, std::string_view t) {
        const std::string v = visible(t);
        return (outline_of(s) ? fonts->advance(font_of(s), v, h) : text_width(v, h) * wf) * s.width * s.tracking;
    };
    constexpr double kStack = 0.7; // a stacked fraction's text, of the run's height
    struct Tok {
        const Item* item = nullptr;
        std::string text;
        bool space = false;
        double w = 0.0;
    };
    const auto tok_width = [&](const Item& it, const std::string& t) {
        if (it.kind == Item::Kind::Stack) {
            const double sh = it.style.height * kStack;
            const double wt = measure(it.style, sh, it.top);
            const double wb = measure(it.style, sh, it.bottom);
            return it.stack == '#' ? wt + wb + 0.3 * it.style.height : std::max(wt, wb) + 0.1 * it.style.height;
        }
        return measure(it.style, it.style.height, t);
    };
    // Paragraphs of words (runs of tokens with no space between) and spaces.
    struct Para {
        std::vector<std::vector<Tok>> words; // a space is a word of one space token
        double h = 1.0;                      // its first run's height (an empty paragraph's)
    };
    std::vector<Para> paras(1);
    paras.back().h = height;
    bool para_started = false;
    bool joining = false; // the next token continues the word before
    const auto push_tok = [&](Tok t) {
        Para& p = paras.back();
        if (!para_started) {
            p.h = t.item->style.height;
            para_started = true;
        }
        if (t.space || !joining || p.words.empty()) {
            p.words.push_back({});
        }
        p.words.back().push_back(std::move(t));
        joining = !p.words.back().back().space;
    };
    for (const Item& it : items) {
        if (it.kind == Item::Kind::Break) {
            if (!para_started) {
                paras.back().h = it.style.height;
            }
            paras.push_back(Para{});
            paras.back().h = it.style.height;
            para_started = false;
            joining = false;
            continue;
        }
        if (it.kind == Item::Kind::Stack) {
            push_tok(Tok{&it, {}, false, tok_width(it, {})});
            continue;
        }
        std::string word;
        const auto end_word = [&] {
            if (!word.empty()) {
                push_tok(Tok{&it, word, false, tok_width(it, word)});
                word.clear();
            }
        };
        for (const char ch : it.text) {
            if (ch == ' ') {
                end_word();
                push_tok(Tok{&it, " ", true, tok_width(it, " ")});
            } else {
                word += ch;
            }
        }
        end_word();
    }
    // Lines: whole words, broken at spaces to the block's width (a word longer than it
    // keeps a line of its own).
    struct Line {
        std::vector<const Tok*> toks;
        double w = 0.0;
        double h = 0.0;
    };
    std::vector<Line> lines;
    for (const Para& p : paras) {
        Line cur;
        cur.h = p.h;
        std::vector<const Tok*> spaces;
        double spaces_w = 0.0;
        for (const std::vector<Tok>& word : p.words) {
            if (word.size() == 1 && word.front().space) {
                spaces.push_back(&word.front());
                spaces_w += word.front().w;
                continue;
            }
            double ww = 0.0;
            double wh = 0.0;
            for (const Tok& t : word) {
                ww += t.w;
                wh = std::max(wh, t.item->style.height);
            }
            if (block.width > 0.0 && !cur.toks.empty() && cur.w + spaces_w + ww > block.width) {
                lines.push_back(std::move(cur));
                cur = Line{};
                cur.h = wh;
                spaces.clear();
                spaces_w = 0.0;
            }
            for (const Tok* sp : spaces) {
                cur.toks.push_back(sp);
            }
            cur.w += spaces_w + ww;
            spaces.clear();
            spaces_w = 0.0;
            for (const Tok& t : word) {
                cur.toks.push_back(&t);
            }
            cur.h = cur.toks.size() == word.size() ? wh : std::max(cur.h, wh);
        }
        for (const Tok* sp : spaces) { // trailing spaces stay with their line
            cur.toks.push_back(sp);
            cur.w += sp->w;
        }
        lines.push_back(std::move(cur));
    }
    constexpr double kSingleSpacing = 5.0 / 3.0;
    const double spacing = block.line_spacing > 0.0 ? block.line_spacing : 1.0;
    double maxw = 0.0;
    double total_h = 0.0;
    for (const Line& l : lines) {
        maxw = std::max(maxw, l.w);
        total_h += l.h * kSingleSpacing * spacing;
    }
    out.line_count = static_cast<int>(lines.size());
    const double box_w = block.width > 0.0 ? block.width : maxw;
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
    extend(to_world(0.0, 0.0));
    extend(to_world(box_w, 0.0));
    extend(to_world(0.0, -total_h));
    extend(to_world(box_w, -total_h));
    const auto line_seg = [&](double x0, double y0, double x1, double y1) {
        const Vec2 a = to_world(x0, y0);
        const Vec2 b = to_world(x1, y1);
        out.segments.push_back(a);
        out.segments.push_back(b);
        extend(a);
        extend(b);
    };
    std::vector<Vec2> glyphs;
    // A run of text at (x, baseline) local, `h` tall, in its style.
    const auto draw_text = [&](const RunStyle& s, const std::string& t, double x, double baseline, double h) {
        const std::string v = visible(t);
        if (outline_of(s)) {
            const std::size_t before = out.fills.size();
            fonts->glyph_fills(font_of(s), v, to_world(x, baseline), h, block.rotation, out.fills);
            for (std::size_t k = before; k < out.fills.size(); ++k) {
                extend(out.fills[k]);
            }
            return;
        }
        const double sx = wf * s.width;
        const double slant = std::tan(s.oblique);
        const auto put = [&](std::string_view piece, double px) {
            glyphs.clear();
            append_text_segments(piece, {0.0, 0.0}, h, 0.0, Justify::Left, glyphs);
            for (const Vec2& g : glyphs) {
                const Vec2 w = to_world(px + g.x * sx + g.y * slant, baseline + g.y);
                out.segments.push_back(w);
                extend(w);
            }
        };
        if (s.tracking == 1.0) {
            put(v, x);
            return;
        }
        // Tracking spreads the letters apart without widening them: one at a time.
        double px = x;
        for (std::size_t i = 0; i < v.size();) {
            const auto lead = static_cast<unsigned char>(v[i]);
            const std::size_t n = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
            const std::string_view ch = std::string_view(v).substr(i, n);
            put(ch, px);
            px += text_width(ch, h) * sx * s.tracking;
            i += n;
        }
    };
    double top = 0.0;
    for (const Line& l : lines) {
        const double lh = l.h * kSingleSpacing * spacing;
        const double baseline = top - l.h;
        const double line_x = (col == 1) ? (box_w - l.w) * 0.5 : (col == 2 ? box_w - l.w : 0.0);
        std::string plain;
        double x = line_x;
        for (const Tok* t : l.toks) {
            const RunStyle& s = t->item->style;
            const double h = s.height;
            const std::size_t seg0 = out.segments.size();
            const std::size_t fill0 = out.fills.size();
            if (t->item->kind == Item::Kind::Stack) {
                const Item& it = *t->item;
                const double sh = h * kStack;
                const double wt = measure(s, sh, it.top);
                const double wb = measure(s, sh, it.bottom);
                const double bar = baseline + 0.45 * h;
                if (it.stack == '#') { // diagonal: the top raised on the left, the bottom lowered on the right
                    draw_text(s, it.top, x, bar + 0.1 * h, sh);
                    line_seg(x + wt + 0.05 * h, baseline - 0.2 * h, x + wt + 0.25 * h, baseline + h);
                    draw_text(s, it.bottom, x + wt + 0.3 * h, baseline - 0.2 * h, sh);
                } else if (it.stack == '^') { // a tolerance: one over the other, no bar, flush left
                    draw_text(s, it.top, x, bar + 0.1 * h, sh);
                    draw_text(s, it.bottom, x, bar - 0.1 * h - sh, sh);
                } else { // a fraction: centred over and under a bar
                    const double sw = t->w;
                    draw_text(s, it.top, x + (sw - wt) * 0.5, bar + 0.15 * h, sh);
                    line_seg(x, bar, x + sw, bar);
                    draw_text(s, it.bottom, x + (sw - wb) * 0.5, bar - 0.15 * h - sh, sh);
                }
                plain += it.top + (it.bottom.empty() ? "" : "/" + it.bottom);
            } else {
                if (!t->space) {
                    draw_text(s, t->text, x, baseline, h);
                }
                plain += t->space ? std::string(" ") : visible(t->text);
            }
            if (s.underline) {
                line_seg(x, baseline - 0.2 * h, x + t->w, baseline - 0.2 * h);
            }
            if (s.overline) {
                line_seg(x, baseline + 1.2 * h, x + t->w, baseline + 1.2 * h);
            }
            if (s.strike) {
                line_seg(x, baseline + 0.5 * h, x + t->w, baseline + 0.5 * h);
            }
            if (s.colored && (out.segments.size() > seg0 || out.fills.size() > fill0)) {
                out.colors.push_back(MTextColorSpan{seg0, out.segments.size(), fill0, out.fills.size(), s.color});
            }
            x += t->w;
        }
        out.lines.push_back(MTextLine{plain, to_world(line_x, baseline)});
        top -= lh;
    }
    if (first) {
        out.min = out.max = block.pos;
    }
    return out;
}

} // namespace

MTextLayout layout_mtext(const MTextBlock& block, std::string_view raw_content,
                         const IFontEngine* fonts, std::string_view font_name) {
    // Content with format codes (from AutoCAD, or typed so) is laid out run by run; the
    // fields expand first, their own syntax having backslashes of its own.
    if (raw_content.find('\\') != std::string_view::npos) {
        const std::string expanded = expand_fields(raw_content);
        if (has_mtext_codes(expanded)) {
            return layout_formatted(block, expanded, fonts, font_name);
        }
    }
    MTextLayout out;
    // Expand control codes (%%c, %%d, %%p, %%b, %%h, %%v, %%nnn, and the \U+XXXX escape) to
    // Unicode before wrap/measure/emit so the rendered glyphs and the wrap box reflect the
    // visible text. Storage stays the raw string (derived-not-baked). Overline/underline
    // toggles are stripped here (the single-line TEXT path draws the bars); MTEXT decoration
    // is deferred.
    const std::string content = substitute_text(raw_content);
    const double height = block.height > 0.0 ? block.height : 1.0;
    const double wf = block.width_factor > 0.0 ? block.width_factor : 1.0;
    // An outline (TTF) face emits filled glyphs and uses its own advances; the stroke font
    // emits line segments and applies the width factor. Either way the layout (wrap/attach/
    // spacing) is shared -- only the per-glyph source + width metric differ.
    const bool outline = fonts != nullptr && fonts->is_outline_font(font_name);
    const auto measure = [&](std::string_view s) -> double {
        return outline ? fonts->advance(font_name, s, height) : text_width(s, height) * wf;
    };
    // AutoCAD MTEXT single spacing puts baselines 5/3 of the cap height apart (a ~0.67h
    // gap between lines); stacking exactly one cap height apart makes lines touch/overlap.
    constexpr double kSingleSpacing = 5.0 / 3.0;
    const double line_height =
        height * kSingleSpacing * (block.line_spacing > 0.0 ? block.line_spacing : 1.0);
    const std::vector<std::string> lines = wrap_lines(content, block.width, measure);
    const int n = static_cast<int>(lines.size());
    out.line_count = n;

    double maxw = 0.0;
    for (const std::string& l : lines) {
        maxw = std::max(maxw, measure(l));
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
        const double linew = measure(lines[i]);
        const double line_x = (col == 1) ? (box_w - linew) * 0.5 : (col == 2 ? box_w - linew : 0.0);
        const double baseline = -static_cast<double>(i) * line_height - height;
        out.lines.push_back(MTextLine{lines[i], to_world(line_x, baseline)});
        if (outline) {
            // Outline glyphs are produced in world space directly (origin + rotation);
            // width_factor is not applied to TTF (a noted minor gap).
            const std::size_t before = out.fills.size();
            fonts->glyph_fills(font_name, lines[i], to_world(line_x, baseline), height,
                               block.rotation, out.fills);
            for (std::size_t k = before; k < out.fills.size(); ++k) {
                extend(out.fills[k]);
            }
        } else {
            glyphs.clear();
            append_text_segments(lines[i], {0.0, 0.0}, height, 0.0, Justify::Left, glyphs);
            for (const Vec2& g : glyphs) {
                const Vec2 w = to_world(line_x + g.x * wf, baseline + g.y);
                out.segments.push_back(w);
                extend(w);
            }
        }
    }
    if (first) { // no content/box: degenerate
        out.min = out.max = block.pos;
    }
    return out;
}

} // namespace musacad::core::text
