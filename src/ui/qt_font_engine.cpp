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

// Signed area of a polygon (px space). Positive = CCW in a y-up frame.
double signed_area(const std::vector<Vec2>& p) {
    double a = 0.0;
    for (std::size_t i = 0, n = p.size(); i < n; ++i) {
        const Vec2& u = p[i];
        const Vec2& v = p[(i + 1) % n];
        a += u.x * v.y - v.x * u.y;
    }
    return 0.5 * a;
}

bool point_in_tri(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& p) {
    const double d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
    const double d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
    const double d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
    const bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos); // all same sign (or on edge) => inside
}

// Ear-clip a (weakly-)simple polygon into triangles. `poly` comes from
// QPainterPath::toFillPolygon(), which bridges holes into one outline, so we triangulate
// a single ring. A force-progress guard prevents stalls on the zero-area bridge slivers.
void ear_clip(std::vector<Vec2> v, std::vector<Vec2>& out) {
    if (v.size() < 3) {
        return;
    }
    if (signed_area(v) < 0.0) {
        std::reverse(v.begin(), v.end()); // orient CCW so the convexity test is consistent
    }
    std::vector<std::size_t> idx(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        idx[i] = i;
    }
    std::size_t guard = 0;
    const std::size_t guard_max = idx.size() * idx.size() + 16;
    while (idx.size() > 3 && guard++ < guard_max) {
        bool clipped = false;
        const std::size_t n = idx.size();
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t ia = idx[(i + n - 1) % n];
            const std::size_t ib = idx[i];
            const std::size_t ic = idx[(i + 1) % n];
            const Vec2& a = v[ia];
            const Vec2& b = v[ib];
            const Vec2& c = v[ic];
            const double cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (cross <= 0.0) {
                continue; // reflex or collinear: not an ear
            }
            bool ear = true;
            for (std::size_t j = 0; j < n; ++j) {
                const std::size_t ip = idx[j];
                if (ip == ia || ip == ib || ip == ic) {
                    continue;
                }
                if (point_in_tri(a, b, c, v[ip])) {
                    ear = false;
                    break;
                }
            }
            if (!ear) {
                continue;
            }
            out.push_back(a);
            out.push_back(b);
            out.push_back(c);
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) {
            // No ear found (degenerate/bridge sliver): drop the sharpest vertex to progress.
            idx.erase(idx.begin());
        }
    }
    if (idx.size() == 3) {
        out.push_back(v[idx[0]]);
        out.push_back(v[idx[1]]);
        out.push_back(v[idx[2]]);
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
    const std::string sans = pick({"DejaVu Sans", "Noto Sans", "Liberation Sans", "Arial"});
    const std::string serif = pick({"DejaVu Serif", "Noto Serif", "Liberation Serif", "Times New Roman"});
    const std::string mono = pick({"DejaVu Sans Mono", "Liberation Mono", "Courier New"});
    const auto add = [&](const char* shx, const std::string& fam) {
        if (!fam.empty()) {
            subst_[shx] = fam;
        }
    };
    add("txt", sans);
    add("simplex", sans);
    add("romans", serif.empty() ? sans : serif);
    add("romand", serif.empty() ? sans : serif);
    add("romanc", serif.empty() ? sans : serif);
    add("italic", serif.empty() ? sans : serif);
    add("isocp", sans);
    add("isocpeur", sans);
    add("iso", sans);
    add("monotxt", mono.empty() ? sans : mono);
    add("arial", pick({"Arial", "Liberation Sans", "DejaVu Sans"}));
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
        const QPolygonF poly = path.toFillPolygon(); // holes bridged into one ring
        std::vector<Vec2> ring;
        ring.reserve(static_cast<std::size_t>(poly.size()));
        for (const QPointF& p : poly) {
            ring.push_back({p.x(), p.y()}); // px, y-down
        }
        std::vector<Vec2> tris_px;
        ear_clip(std::move(ring), tris_px);
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
    // Strip a trailing ".shx" / ".ttf" / ".otf" extension for the table lookup.
    for (const char* ext : {".shx", ".ttf", ".otf"}) {
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
