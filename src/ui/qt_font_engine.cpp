#include "musacad/ui/qt_font_engine.hpp"

#include <algorithm>
#include <cmath>

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QPolygonF>
#include <QRawFont>
#include <QString>

namespace musacad::ui {

namespace {

using core::Vec2;

constexpr double kPixelSize = 256.0; // QFont pixel size used for outline extraction

std::string to_lower(std::string_view s) {
    std::string o(s);
    std::transform(o.begin(), o.end(), o.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return o;
}

// Fill a glyph (outer rings + holes) by horizontal scanlines using the even-odd rule:
// at each band's mid-y, collect the x where the line crosses every contour edge, sort
// them, and the [x0,x1],[x2,x3],... pairs are the filled spans (holes drop out naturally
// -- no bridging, no ear-clipping, no per-glyph special cases, so it never corrupts).
// Each span becomes a quad (two triangles). Bands are fine enough that the horizontal
// stepping is sub-pixel at any realistic text size.
void triangulate_glyph(const std::vector<std::vector<Vec2>>& contours, std::vector<Vec2>& out) {
    double ymin = 1e300;
    double ymax = -1e300;
    for (const std::vector<Vec2>& c : contours) {
        for (const Vec2& p : c) {
            ymin = std::min(ymin, p.y);
            ymax = std::max(ymax, p.y);
        }
    }
    if (!(ymax > ymin)) {
        return;
    }
    constexpr int kBands = 96; // over the glyph height (cached once at unit em)
    const double dy = (ymax - ymin) / kBands;
    std::vector<double> xs;
    for (int row = 0; row < kBands; ++row) {
        const double y0 = ymin + static_cast<double>(row) * dy;
        const double y1 = y0 + dy;
        const double ym = 0.5 * (y0 + y1);
        xs.clear();
        for (const std::vector<Vec2>& c : contours) {
            const std::size_t m = c.size();
            for (std::size_t k = 0; k < m; ++k) {
                const Vec2 a = c[k];
                const Vec2 b = c[(k + 1) % m];
                // Half-open test (a.y <= ym < b.y or vice versa) so shared vertices count once.
                if ((a.y <= ym) != (b.y <= ym)) {
                    xs.push_back(a.x + (ym - a.y) / (b.y - a.y) * (b.x - a.x));
                }
            }
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
            const double xa = xs[k];
            const double xb = xs[k + 1];
            if (xb - xa < 1e-9) {
                continue;
            }
            out.push_back({xa, y0});
            out.push_back({xb, y0});
            out.push_back({xb, y1});
            out.push_back({xa, y0});
            out.push_back({xb, y1});
            out.push_back({xa, y1});
        }
    }
}

} // namespace

QtFontEngine::QtFontEngine() {
    available_.emplace_back("Standard"); // the built-in stroke font
    const QStringList families = QFontDatabase::families();
    for (const QString& fam : families) {
        if (QFontDatabase::isPrivateFamily(fam)) {
            continue;
        }
        available_.push_back(fam.toStdString());
    }
    // SHX font-name -> a sensible TTF lookalike. Single-stroke CAD fonts map to a clean
    // sans; the roman/iso serif-ish faces map to a serif if present. Keys are lowercased
    // (with and without the ".shx" suffix handled at lookup).
    const auto pick = [&](std::initializer_list<const char*> prefs) -> std::string {
        for (const char* p : prefs) {
            if (QFontDatabase::families().contains(QString::fromLatin1(p), Qt::CaseInsensitive)) {
                return p;
            }
        }
        return {};
    };
    // Substitution table for common proprietary TTF *families* not installed by name
    // (e.g. "arial.ttf" -> Liberation/DejaVu Sans). Single-stroke SHX shape fonts are NOT
    // here: substitute() short-circuits every *.shx to the built-in stroke font, which is
    // their faithful single-stroke representation (a filled TTF looks wrong for them).
    const auto add = [&](const char* alias, const std::string& fam) {
        if (!fam.empty()) {
            subst_[alias] = fam;
        }
    };
    add("arial", pick({"Arial", "Liberation Sans", "DejaVu Sans"}));
    add("helvetica", pick({"Helvetica", "Liberation Sans", "DejaVu Sans"}));
    add("times new roman", pick({"Times New Roman", "Liberation Serif", "DejaVu Serif"}));
    add("courier new", pick({"Courier New", "Liberation Mono", "DejaVu Sans Mono"}));
}

bool QtFontEngine::is_outline_font(std::string_view name) const {
    if (name.empty()) {
        return false; // the stroke font
    }
    std::lock_guard lock(mu_);
    return face_for(name) != nullptr;
}

QtFontEngine::Face* QtFontEngine::face_for(std::string_view name) const {
    // Caller holds mu_. Resolve `name` (a family or an SHX/substituted name) to a Face.
    if (name.empty() || to_lower(name) == "standard") {
        return nullptr;
    }
    if (const auto it = faces_.find(name); it != faces_.end()) {
        return it->second.cap > 0.0 ? &it->second : nullptr;
    }
    // Resolve the family: exact family match, else substitution, else give up (stroke).
    QString family = QString::fromUtf8(name.data(), static_cast<int>(name.size()));
    if (!QFontDatabase::families().contains(family, Qt::CaseInsensitive)) {
        const std::string sub = substitute(name);
        if (sub.empty()) {
            faces_[std::string(name)] = Face{QFont{}, -1.0, {}}; // negative cap = "unresolved"
            return nullptr;
        }
        family = QString::fromStdString(sub);
    }
    QFont f(family);
    f.setPixelSize(static_cast<int>(kPixelSize));
    const QFontMetricsF fm(f);
    double cap = fm.capHeight();
    if (!(cap > 0.0)) {
        cap = fm.ascent() * 0.7;
    }
    if (!(cap > 0.0)) {
        cap = kPixelSize * 0.7;
    }
    Face& face = faces_[std::string(name)];
    face.font = f;
    face.cap = cap;
    return &face;
}

const QtFontEngine::Glyph& QtFontEngine::glyph_for(Face& f, char32_t cp) const {
    // Caller holds mu_.
    if (const auto it = f.cache.find(cp); it != f.cache.end()) {
        return it->second;
    }
    Glyph g;
    const QString s = QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
    const QFontMetricsF fm(f.font);
    g.advance = fm.horizontalAdvance(s) / f.cap; // unit advance (cap height = 1)
    const QRawFont raw = QRawFont::fromFont(f.font);
    const QList<quint32> idx = raw.glyphIndexesForString(s);
    if (!idx.isEmpty()) {
        const QPainterPath path = raw.pathForGlyph(idx.front());
        // Clean closed contours (outer rings + holes), NOT the seam-bridged single ring --
        // the triangulator handles holes by even-odd nesting.
        const QList<QPolygonF> subs = path.toSubpathPolygons();
        std::vector<std::vector<Vec2>> contours;
        contours.reserve(static_cast<std::size_t>(subs.size()));
        for (const QPolygonF& poly : subs) {
            std::vector<Vec2> ring;
            ring.reserve(static_cast<std::size_t>(poly.size()));
            for (const QPointF& p : poly) {
                ring.push_back({p.x(), p.y()}); // px, y-down
            }
            // toSubpathPolygons repeats the first point to close; drop the duplicate.
            if (ring.size() >= 2 && std::abs(ring.front().x - ring.back().x) < 1e-9 &&
                std::abs(ring.front().y - ring.back().y) < 1e-9) {
                ring.pop_back();
            }
            contours.push_back(std::move(ring));
        }
        std::vector<Vec2> tris_px;
        triangulate_glyph(std::move(contours), tris_px);
        g.tris.reserve(tris_px.size());
        for (const Vec2& p : tris_px) {
            g.tris.push_back({p.x / f.cap, -p.y / f.cap}); // normalise + flip to y-up
        }
    }
    return f.cache.emplace(cp, std::move(g)).first->second;
}

double QtFontEngine::advance(std::string_view name, std::string_view text, double height) const {
    std::lock_guard lock(mu_);
    Face* f = face_for(name);
    if (f == nullptr) {
        return 0.0;
    }
    const QString s = QString::fromUtf8(text.data(), static_cast<int>(text.size()));
    double adv = 0.0;
    for (const char32_t cp : s.toUcs4()) {
        adv += glyph_for(*f, cp).advance;
    }
    return adv * height;
}

void QtFontEngine::glyph_fills(std::string_view name, std::string_view text, Vec2 origin,
                               double height, double rotation, std::vector<Vec2>& tris) const {
    std::lock_guard lock(mu_);
    Face* f = face_for(name);
    if (f == nullptr) {
        return;
    }
    const double cs = std::cos(rotation);
    const double sn = std::sin(rotation);
    const QString s = QString::fromUtf8(text.data(), static_cast<int>(text.size()));
    double pen = 0.0; // advance along the (unrotated) baseline, in world units
    for (const char32_t cp : s.toUcs4()) {
        const Glyph& g = glyph_for(*f, cp);
        for (const Vec2& v : g.tris) {
            const double lx = pen + v.x * height; // local (baseline) coords
            const double ly = v.y * height;
            tris.push_back({origin.x + lx * cs - ly * sn, origin.y + lx * sn + ly * cs});
        }
        pen += g.advance * height;
    }
}

std::vector<std::string> QtFontEngine::available() const { return available_; }

std::string QtFontEngine::substitute(std::string_view requested) const {
    if (requested.empty()) {
        return {};
    }
    std::string key = to_lower(requested);
    // Single-stroke SHX shape fonts (txt/simplex/romans/isocp/...) are faithfully drawn by
    // the built-in single-stroke font; a filled TTF substitute looks wrong and loses the CAD
    // look. Map every *.shx to the stroke font ("") so imported text matches the original.
    if (key.size() >= 4 && key.compare(key.size() - 4, 4, ".shx") == 0) {
        return {};
    }
    // Strip a trailing ".ttf" / ".otf" extension for the table lookup.
    for (const char* ext : {".ttf", ".otf"}) {
        const std::string e(ext);
        if (key.size() > e.size() && key.compare(key.size() - e.size(), e.size(), e) == 0) {
            key.erase(key.size() - e.size());
            break;
        }
    }
    // A TTF family that exists by name resolves to itself.
    const QString fam = QString::fromUtf8(requested.data(), static_cast<int>(requested.size()));
    if (QFontDatabase::families().contains(fam, Qt::CaseInsensitive)) {
        return std::string(requested);
    }
    if (const auto it = subst_.find(key); it != subst_.end()) {
        return it->second;
    }
    return {}; // nothing matched -> caller uses the stroke font
}

} // namespace musacad::ui
