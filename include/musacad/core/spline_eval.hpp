// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "musacad/core/math/math.hpp"

// B-spline evaluation shared by everything that touches a SPLINE: the kernel's
// tessellation (drawing, picking, bounds), the SPLINE command's live preview, DXF
// export (the knot vector written is the one drawn), and FIT-point interpolation --
// which must use the very same basis, or the committed curve would not pass through
// the points the preview showed it passing through.
//
// A spline drawn in Musa CAD is a clamped B-spline with uniform interior knots on [0,1]
// unless it carries knots of its own (a spline from an AutoCAD drawing, or a piece of a
// trimmed or broken one); `degree` is lowered to count-1 when there are too few control
// points, as AutoCAD does.

namespace musacad::core::spline {

inline int effective_degree(int control_count, int degree) noexcept {
    return std::clamp(degree, 1, std::max(1, control_count - 1));
}

inline std::vector<double> clamped_knots(int control_count, int degree) {
    const int knot_count = control_count + degree + 1;
    std::vector<double> u(static_cast<std::size_t>(knot_count), 0.0);
    const int interior = control_count - degree - 1; // may be <= 0
    for (int j = 1; j <= interior; ++j) {
        u[static_cast<std::size_t>(degree + j)] =
            static_cast<double>(j) / static_cast<double>(control_count - degree);
    }
    for (int i = control_count; i < knot_count; ++i) {
        u[static_cast<std::size_t>(i)] = 1.0;
    }
    return u;
}

/// Knot span index for parameter t (n = control_count - 1).
inline int find_span(int n, int degree, double t, const std::vector<double>& u) noexcept {
    if (t >= u[static_cast<std::size_t>(n + 1)]) {
        return n;
    }
    if (t <= u[static_cast<std::size_t>(degree)]) {
        return degree;
    }
    int low = degree;
    int high = n + 1;
    int mid = (low + high) / 2;
    while (t < u[static_cast<std::size_t>(mid)] || t >= u[static_cast<std::size_t>(mid + 1)]) {
        if (t < u[static_cast<std::size_t>(mid)]) {
            high = mid;
        } else {
            low = mid;
        }
        mid = (low + high) / 2;
    }
    return mid;
}

/// The degree+1 non-zero basis functions N_{span-degree..span}(t) (Cox-de Boor).
inline void basis_functions(int span, double t, int degree, const std::vector<double>& u,
                            std::vector<double>& N) {
    N.assign(static_cast<std::size_t>(degree + 1), 0.0);
    std::vector<double> left(static_cast<std::size_t>(degree + 1), 0.0);
    std::vector<double> right(static_cast<std::size_t>(degree + 1), 0.0);
    N[0] = 1.0;
    for (int j = 1; j <= degree; ++j) {
        left[static_cast<std::size_t>(j)] = t - u[static_cast<std::size_t>(span + 1 - j)];
        right[static_cast<std::size_t>(j)] = u[static_cast<std::size_t>(span + j)] - t;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            const double denom = right[static_cast<std::size_t>(r + 1)] + left[static_cast<std::size_t>(j - r)];
            const double temp = denom != 0.0 ? N[static_cast<std::size_t>(r)] / denom : 0.0;
            N[static_cast<std::size_t>(r)] = saved + right[static_cast<std::size_t>(r + 1)] * temp;
            saved = left[static_cast<std::size_t>(j - r)] * temp;
        }
        N[static_cast<std::size_t>(j)] = saved;
    }
}

inline Vec2 de_boor(int span, double t, int degree, const std::vector<double>& u,
                    std::span<const Vec2> ctrl) {
    std::vector<Vec2> d(static_cast<std::size_t>(degree + 1));
    for (int j = 0; j <= degree; ++j) {
        d[static_cast<std::size_t>(j)] = ctrl[static_cast<std::size_t>(span - degree + j)];
    }
    for (int r = 1; r <= degree; ++r) {
        for (int j = degree; j >= r; --j) {
            const double left = u[static_cast<std::size_t>(span - degree + j)];
            const double right = u[static_cast<std::size_t>(span + 1 + j - r)];
            const double denom = right - left;
            const double alpha = denom > 0.0 ? (t - left) / denom : 0.0;
            d[static_cast<std::size_t>(j)] =
                d[static_cast<std::size_t>(j - 1)] * (1.0 - alpha) + d[static_cast<std::size_t>(j)] * alpha;
        }
    }
    return d[static_cast<std::size_t>(degree)];
}

/// Knots a spline of `count` control points and (effective) `degree` can be drawn with:
/// count + degree + 1 values, never decreasing, over a domain of some length.
inline bool usable_knots(std::span<const double> k, int count, int degree) noexcept {
    if (count < 2 || degree < 1 || k.size() != static_cast<std::size_t>(count + degree + 1)) {
        return false;
    }
    for (std::size_t i = 1; i < k.size(); ++i) {
        if (!(k[i] >= k[i - 1])) { // NaN fails too
            return false;
        }
    }
    return k[static_cast<std::size_t>(degree)] < k[static_cast<std::size_t>(count)];
}

/// A spline ready to evaluate: its control points, the degree it is drawn with, and the
/// knots -- its own when they fit it, otherwise the clamped uniform ones.
struct Curve {
    std::span<const Vec2> ctrl;
    int degree = 1;
    std::vector<double> knots;
    bool own = false; ///< drawn with knots of its own

    Curve(std::span<const Vec2> c, int degree_in, std::span<const double> own_knots = {})
        : ctrl(c), degree(effective_degree(static_cast<int>(c.size()), degree_in)) {
        const int count = static_cast<int>(c.size());
        if (count < 2) {
            return;
        }
        own = usable_knots(own_knots, count, degree);
        knots = own ? std::vector<double>(own_knots.begin(), own_knots.end()) : clamped_knots(count, degree);
    }
    [[nodiscard]] bool valid() const noexcept { return ctrl.size() >= 2; }
    /// The domain: the parameters at the curve's start and end.
    [[nodiscard]] double lo() const noexcept { return knots[static_cast<std::size_t>(degree)]; }
    [[nodiscard]] double hi() const noexcept { return knots[ctrl.size()]; }
    /// The point at knot parameter u (clamped to the domain).
    [[nodiscard]] Vec2 at(double u) const {
        if (ctrl.empty()) {
            return {};
        }
        if (ctrl.size() == 1) {
            return ctrl[0];
        }
        u = std::clamp(u, lo(), hi());
        return de_boor(find_span(static_cast<int>(ctrl.size()) - 1, degree, u, knots), u, degree, knots, ctrl);
    }
    /// The point at t in [0,1] along the domain.
    [[nodiscard]] Vec2 at_unit(double t) const {
        if (ctrl.size() < 2) {
            return at(0.0);
        }
        return at(lo() + (hi() - lo()) * std::clamp(t, 0.0, 1.0));
    }
};

/// The curve point at t in [0,1] (along the domain of its knots).
inline Vec2 evaluate(std::span<const Vec2> ctrl, int degree_in, double t, std::span<const double> own = {}) {
    return Curve(ctrl, degree_in, own).at_unit(t);
}

/// Chords along the curve: 16 samples per span, bounded -- the rule every consumer draws
/// and picks with. A curve with knots of its own is sampled span by span, so a short span
/// gets as many chords as a long one.
inline void tessellate(std::span<const Vec2> ctrl, std::uint32_t degree_in, std::vector<Vec2>& out,
                       std::span<const double> own = {}) {
    const int count = static_cast<int>(ctrl.size());
    if (count <= 0) {
        return;
    }
    if (count == 1) {
        out.push_back(ctrl[0]);
        return;
    }
    const Curve c(ctrl, static_cast<int>(degree_in), own);
    const auto samples = std::clamp<std::size_t>(static_cast<std::size_t>(count - 1) * 16,
                                                 std::size_t{2}, std::size_t{8192});
    if (!c.own) {
        out.reserve(out.size() + samples + 1);
        for (std::size_t i = 0; i <= samples; ++i) {
            out.push_back(c.at_unit(static_cast<double>(i) / static_cast<double>(samples)));
        }
        return;
    }
    std::size_t spans = 0;
    for (int i = c.degree; i < count; ++i) {
        spans += c.knots[static_cast<std::size_t>(i + 1)] > c.knots[static_cast<std::size_t>(i)] ? 1u : 0u;
    }
    const std::size_t per = std::clamp<std::size_t>(samples / std::max<std::size_t>(spans, 1), 2, 16);
    out.reserve(out.size() + spans * per + 1);
    out.push_back(c.at(c.lo()));
    for (int i = c.degree; i < count; ++i) {
        const double a = c.knots[static_cast<std::size_t>(i)];
        const double b = c.knots[static_cast<std::size_t>(i + 1)];
        if (!(b > a)) {
            continue;
        }
        for (std::size_t k = 1; k <= per; ++k) {
            out.push_back(c.at(a + (b - a) * static_cast<double>(k) / static_cast<double>(per)));
        }
    }
}

/// The parameter (knot parameter, within the domain) of the curve point nearest p: the
/// nearest of a dense sampling, refined between its neighbours.
inline double param_near(const Curve& c, Vec2 p) {
    if (!c.valid()) {
        return 0.0;
    }
    const int n = std::clamp(static_cast<int>(c.ctrl.size()) * 32, 64, 8192);
    const double a = c.lo();
    const double b = c.hi();
    const auto u_of = [&](int i) { return a + (b - a) * static_cast<double>(i) / static_cast<double>(n); };
    int best = 0;
    double bd = length_squared(c.at(a) - p);
    for (int i = 1; i <= n; ++i) {
        const double d = length_squared(c.at(u_of(i)) - p);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    double l = u_of(std::max(best - 1, 0));
    double r = u_of(std::min(best + 1, n));
    for (int it = 0; it < 60; ++it) { // golden-section on the distance
        const double m1 = l + (r - l) * 0.381966;
        const double m2 = l + (r - l) * 0.618034;
        if (length_squared(c.at(m1) - p) < length_squared(c.at(m2) - p)) {
            r = m2;
        } else {
            l = m1;
        }
    }
    return 0.5 * (l + r);
}

/// One knot inserted at u (Boehm): one more control point, the curve unchanged.
inline void insert_knot(std::vector<Vec2>& P, std::vector<double>& U, int p, double u) {
    const int n = static_cast<int>(P.size()) - 1;
    const int k = find_span(n, p, u, U);
    std::vector<Vec2> Q(P.size() + 1);
    for (int i = 0; i <= n + 1; ++i) {
        const auto ui = static_cast<std::size_t>(i);
        if (i <= k - p) {
            Q[ui] = P[ui];
        } else if (i > k) {
            Q[ui] = P[ui - 1];
        } else {
            const double den = U[static_cast<std::size_t>(i + p)] - U[ui];
            const double alpha = den > 0.0 ? (u - U[ui]) / den : 0.0;
            Q[ui] = P[ui] * alpha + P[ui - 1] * (1.0 - alpha);
        }
    }
    U.insert(U.begin() + k + 1, u);
    P = std::move(Q);
}

/// The knots left when control point i of a spline with knots U and degree p is taken out:
/// the interior knot nearest that point's place along the curve (its Greville abscissa)
/// goes with it. Empty when there is no interior knot to drop.
inline std::vector<double> knots_without(std::span<const double> U, int p, std::size_t i) {
    const std::size_t count = U.size() - static_cast<std::size_t>(p) - 1;
    if (count <= static_cast<std::size_t>(p) + 1 || i >= count) {
        return {};
    }
    double g = 0.0;
    for (int j = 1; j <= p; ++j) {
        g += U[i + static_cast<std::size_t>(j)];
    }
    g /= static_cast<double>(p);
    std::size_t drop = static_cast<std::size_t>(p) + 1;
    for (std::size_t j = drop; j < count; ++j) {
        if (std::abs(U[j] - g) < std::abs(U[drop] - g)) {
            drop = j;
        }
    }
    std::vector<double> out(U.begin(), U.end());
    out.erase(out.begin() + static_cast<std::ptrdiff_t>(drop));
    return out;
}

/// A spline of its own: control points and knots (a piece of a cut one).
struct Piece {
    std::vector<Vec2> ctrl;
    std::vector<double> knots;
};

/// The curve cut at knot parameter u (strictly inside the domain) into the part before and
/// the part after: u inserted until it is `degree` deep (Boehm), which puts a control point
/// on the curve there, shared by the two pieces. Each piece is the same curve over its part,
/// of the same degree, clamped at the cut. False when u is not inside the domain.
inline bool split(const Curve& c, double u, Piece& before, Piece& after) {
    if (!c.valid() || !(u > c.lo()) || !(u < c.hi())) {
        return false;
    }
    const int p = c.degree;
    std::vector<Vec2> P(c.ctrl.begin(), c.ctrl.end());
    std::vector<double> U = c.knots;
    const double eps = 1e-12 * std::max(1.0, c.hi() - c.lo());
    for (double& k : U) { // a cut at a knot takes the knot's own value
        if (std::abs(k - u) <= eps) {
            u = k;
        }
    }
    int s = 0;
    for (const double k : U) {
        s += k == u ? 1 : 0;
    }
    for (int r = s; r < p; ++r) {
        insert_knot(P, U, p, u);
    }
    std::size_t j0 = 0; // the first u
    while (j0 < U.size() && U[j0] < u) {
        ++j0;
    }
    if (j0 == 0 || j0 > P.size()) {
        return false;
    }
    std::size_t last = j0; // past the last u
    while (last < U.size() && U[last] == u) {
        ++last;
    }
    const std::size_t m = last - j0; // u's depth: p, or more when it already was
    const auto pp = static_cast<std::size_t>(p);
    // Before: control points up to the one on the curve at u (its limit from below).
    before.ctrl.assign(P.begin(), P.begin() + static_cast<std::ptrdiff_t>(j0));
    before.knots.assign(U.begin(), U.begin() + static_cast<std::ptrdiff_t>(j0));
    before.knots.insert(before.knots.end(), pp + 1, u);
    // After: from the control point on the curve at u.
    const std::size_t first = j0 - 1 + (m > pp ? m - pp : 0);
    after.ctrl.assign(P.begin() + static_cast<std::ptrdiff_t>(first), P.end());
    after.knots.assign(pp + 1, u);
    after.knots.insert(after.knots.end(), U.begin() + static_cast<std::ptrdiff_t>(last), U.end());
    return before.ctrl.size() >= 2 && after.ctrl.size() >= 2 &&
           before.knots.size() == before.ctrl.size() + pp + 1 && after.knots.size() == after.ctrl.size() + pp + 1;
}

/// Two clamped pieces of degree p, the first ending where the second starts, as one spline:
/// the second's knots moved on to follow the first's, the shared point kept once.
inline Piece joined(const Piece& a, const Piece& b, int p) {
    Piece out;
    const auto pp = static_cast<std::size_t>(p);
    if (a.ctrl.empty() || b.ctrl.empty() || a.knots.size() != a.ctrl.size() + pp + 1 ||
        b.knots.size() != b.ctrl.size() + pp + 1) {
        return out;
    }
    out.ctrl = a.ctrl;
    out.ctrl.insert(out.ctrl.end(), b.ctrl.begin() + 1, b.ctrl.end());
    out.knots.assign(a.knots.begin(), a.knots.end() - 1);
    const double shift = a.knots.back() - b.knots.front();
    for (std::size_t i = pp + 1; i < b.knots.size(); ++i) {
        out.knots.push_back(b.knots[i] + shift);
    }
    return out;
}

/// Whether the curve begins and ends on its end control points (its end knots degree+1 deep).
inline bool clamped(const Curve& c) noexcept {
    if (!c.valid()) {
        return false;
    }
    const auto p = static_cast<std::size_t>(c.degree);
    const std::size_t m = c.knots.size() - 1;
    for (std::size_t i = 1; i <= p; ++i) {
        if (c.knots[i] != c.knots[0] || c.knots[m - i] != c.knots[m]) {
            return false;
        }
    }
    return true;
}

/// The length along the curve between knot parameters a and b (a dense chord sum).
inline double length_between(const Curve& c, double a, double b) {
    if (!c.valid() || !(b > a)) {
        return 0.0;
    }
    const int n = std::clamp(static_cast<int>(c.ctrl.size()) * 32, 128, 8192);
    double sum = 0.0;
    Vec2 prev = c.at(a);
    for (int i = 1; i <= n; ++i) {
        const Vec2 q = c.at(a + (b - a) * static_cast<double>(i) / static_cast<double>(n));
        sum += distance(prev, q);
        prev = q;
    }
    return sum;
}

/// The parameter `s` along the curve from its start (or, `from_start` false, back from its end).
inline double param_at_length(const Curve& c, double s, bool from_start) {
    double lo = c.lo();
    double hi = c.hi();
    for (int it = 0; it < 60; ++it) {
        const double mid = 0.5 * (lo + hi);
        const double len = from_start ? length_between(c, c.lo(), mid) : length_between(c, mid, c.hi());
        if ((len < s) == from_start) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

/// The unit direction the curve leaves by at its end (`at_end`) or start -- pointing out of
/// it -- for a clamped curve: along its last (first) leg of distinct control points.
inline std::optional<Vec2> end_direction(const Curve& c, bool at_end) {
    if (!clamped(c)) {
        return std::nullopt;
    }
    const std::size_t n = c.ctrl.size();
    const Vec2 e = at_end ? c.ctrl[n - 1] : c.ctrl[0];
    for (std::size_t k = 1; k < n; ++k) {
        const Vec2 q = at_end ? c.ctrl[n - 1 - k] : c.ctrl[k];
        const double d = distance(e, q);
        if (d > 1e-12) {
            return (e - q) / d;
        }
    }
    return std::nullopt;
}

/// The curve with a straight run added at its end (`at_end`) or start, from there to `to`
/// (on the line it leaves by): AutoCAD's spline extension -- the curve as it was, then a
/// line tangent to it. One spline, its knots its own.
inline bool extended_straight(const Curve& c, bool at_end, Vec2 to, Piece& out) {
    if (!clamped(c)) {
        return false;
    }
    const int p = c.degree;
    const Vec2 e = at_end ? c.ctrl.back() : c.ctrl.front();
    const double len = distance(e, to);
    if (len <= 1e-12) {
        return false;
    }
    const double total = length_between(c, c.lo(), c.hi());
    const double span = (c.hi() - c.lo()) * (total > 1e-12 ? len / total : 1.0);
    Piece line;
    for (int k = 0; k <= p; ++k) {
        const double f = static_cast<double>(k) / static_cast<double>(p);
        line.ctrl.push_back(at_end ? e + (to - e) * f : to + (e - to) * f);
    }
    line.knots.assign(static_cast<std::size_t>(p) + 1, 0.0);
    line.knots.insert(line.knots.end(), static_cast<std::size_t>(p) + 1, span);
    const Piece self{std::vector<Vec2>(c.ctrl.begin(), c.ctrl.end()), c.knots};
    out = at_end ? joined(self, line, p) : joined(line, self, p);
    return out.ctrl.size() >= 2;
}

/// The knots of the same curve run the other way (its control points reversed).
inline std::vector<double> reversed_knots(std::span<const double> k) {
    std::vector<double> out(k.size());
    if (k.empty()) {
        return out;
    }
    const double s = k.front() + k.back();
    for (std::size_t i = 0; i < k.size(); ++i) {
        out[i] = s - k[k.size() - 1 - i];
    }
    return out;
}

/// How FIT points are spaced in parameter space (AutoCAD's Knots option): by chord
/// length, by its square root (centripetal), or uniformly.
enum class FitParam : std::uint8_t { Chord = 0, SquareRoot = 1, Uniform = 2 };

inline std::vector<double> fit_parameters(std::span<const Vec2> q, FitParam mode) {
    const std::size_t n = q.size();
    std::vector<double> u(n, 0.0);
    if (n < 2) {
        return u;
    }
    if (mode == FitParam::Uniform) {
        for (std::size_t i = 0; i < n; ++i) {
            u[i] = static_cast<double>(i) / static_cast<double>(n - 1);
        }
        return u;
    }
    double total = 0.0;
    for (std::size_t i = 1; i < n; ++i) {
        double d = length(q[i] - q[i - 1]);
        if (mode == FitParam::SquareRoot) {
            d = std::sqrt(d);
        }
        total += d;
        u[i] = total;
    }
    if (total <= 1e-12) {
        return fit_parameters(q, FitParam::Uniform);
    }
    for (double& v : u) {
        v /= total;
    }
    u.back() = 1.0;
    return u;
}

/// Global interpolation: the control points of the clamped uniform-knot B-spline of
/// `degree` that passes exactly through every fit point (a square collocation system,
/// solved with partial pivoting). Returns false when the parameterisation makes the
/// system singular -- the caller then retries with FitParam::Uniform, which always
/// satisfies Schoenberg-Whitney for these knots.
inline bool fit_control_points(std::span<const Vec2> fit, int degree_in, FitParam mode,
                               std::vector<Vec2>& ctrl) {
    const int n = static_cast<int>(fit.size());
    ctrl.clear();
    if (n < 2) {
        return false;
    }
    const int degree = effective_degree(n, degree_in);
    const std::vector<double> knots = clamped_knots(n, degree);
    const std::vector<double> u = fit_parameters(fit, mode);
    const auto N = static_cast<std::size_t>(n);
    std::vector<double> A(N * N, 0.0);
    std::vector<double> basis;
    for (int i = 0; i < n; ++i) {
        const int span = find_span(n - 1, degree, u[static_cast<std::size_t>(i)], knots);
        basis_functions(span, u[static_cast<std::size_t>(i)], degree, knots, basis);
        for (int j = 0; j <= degree; ++j) {
            A[static_cast<std::size_t>(i) * N + static_cast<std::size_t>(span - degree + j)] =
                basis[static_cast<std::size_t>(j)];
        }
    }
    std::vector<double> bx(N);
    std::vector<double> by(N);
    for (std::size_t i = 0; i < N; ++i) {
        bx[i] = fit[i].x;
        by[i] = fit[i].y;
    }
    // Gaussian elimination with partial pivoting on [A | bx by].
    for (std::size_t c = 0; c < N; ++c) {
        std::size_t piv = c;
        for (std::size_t r = c + 1; r < N; ++r) {
            if (std::abs(A[r * N + c]) > std::abs(A[piv * N + c])) {
                piv = r;
            }
        }
        if (std::abs(A[piv * N + c]) < 1e-12) {
            return false;
        }
        if (piv != c) {
            for (std::size_t k = 0; k < N; ++k) {
                std::swap(A[c * N + k], A[piv * N + k]);
            }
            std::swap(bx[c], bx[piv]);
            std::swap(by[c], by[piv]);
        }
        for (std::size_t r = c + 1; r < N; ++r) {
            const double f = A[r * N + c] / A[c * N + c];
            if (f == 0.0) {
                continue;
            }
            for (std::size_t k = c; k < N; ++k) {
                A[r * N + k] -= f * A[c * N + k];
            }
            bx[r] -= f * bx[c];
            by[r] -= f * by[c];
        }
    }
    ctrl.assign(N, Vec2{});
    for (std::size_t i = N; i-- > 0;) {
        double sx = bx[i];
        double sy = by[i];
        for (std::size_t k = i + 1; k < N; ++k) {
            sx -= A[i * N + k] * ctrl[k].x;
            sy -= A[i * N + k] * ctrl[k].y;
        }
        ctrl[i] = {sx / A[i * N + i], sy / A[i * N + i]};
    }
    return true;
}

/// Global interpolation with the curve's direction given at its start and/or end (SPLINE's
/// start and end Tangency): one more control point for each, its row asking the curve's
/// derivative there to be the unit direction times the total chord length (the usual
/// magnitude). False when the system is singular, as fit_control_points.
inline bool fit_control_points_tangent(std::span<const Vec2> fit, int degree_in, FitParam mode,
                                       std::optional<Vec2> start_dir, std::optional<Vec2> end_dir,
                                       std::vector<Vec2>& ctrl) {
    const int n = static_cast<int>(fit.size());
    ctrl.clear();
    if (n < 2) {
        return false;
    }
    const auto unit = [](std::optional<Vec2>& d) {
        if (d && length(*d) > 1e-12) {
            *d = *d / length(*d);
        } else {
            d.reset();
        }
    };
    unit(start_dir);
    unit(end_dir);
    const int m = n + (start_dir ? 1 : 0) + (end_dir ? 1 : 0);
    const int degree = effective_degree(m, degree_in);
    const std::vector<double> knots = clamped_knots(m, degree);
    const std::vector<double> u = fit_parameters(fit, mode);
    double chord = 0.0;
    for (std::size_t i = 1; i < fit.size(); ++i) {
        chord += length(fit[i] - fit[i - 1]);
    }
    const auto M = static_cast<std::size_t>(m);
    std::vector<double> A(M * M, 0.0);
    std::vector<double> bx(M, 0.0);
    std::vector<double> by(M, 0.0);
    std::vector<double> basis;
    std::size_t row = 0;
    // A clamped curve's derivative at an end: degree / (first interior knot) times the
    // difference of its first (last) two control points.
    const double d_scale = static_cast<double>(degree) * static_cast<double>(m - degree);
    for (int i = 0; i < n; ++i) {
        const double t = u[static_cast<std::size_t>(i)];
        const int span = find_span(m - 1, degree, t, knots);
        basis_functions(span, t, degree, knots, basis);
        for (int j = 0; j <= degree; ++j) {
            A[row * M + static_cast<std::size_t>(span - degree + j)] = basis[static_cast<std::size_t>(j)];
        }
        bx[row] = fit[static_cast<std::size_t>(i)].x;
        by[row] = fit[static_cast<std::size_t>(i)].y;
        ++row;
        if (i == 0 && start_dir) {
            A[row * M + 0] = -d_scale;
            A[row * M + 1] = d_scale;
            bx[row] = start_dir->x * chord;
            by[row] = start_dir->y * chord;
            ++row;
        }
        if (i == n - 2 && end_dir) {
            // Just before the last fit point's row, so the system stays near-banded.
            A[row * M + (M - 2)] = -d_scale;
            A[row * M + (M - 1)] = d_scale;
            bx[row] = end_dir->x * chord;
            by[row] = end_dir->y * chord;
            ++row;
        }
    }
    for (std::size_t c = 0; c < M; ++c) {
        std::size_t piv = c;
        for (std::size_t r = c + 1; r < M; ++r) {
            if (std::abs(A[r * M + c]) > std::abs(A[piv * M + c])) {
                piv = r;
            }
        }
        if (std::abs(A[piv * M + c]) < 1e-12) {
            return false;
        }
        if (piv != c) {
            for (std::size_t k = 0; k < M; ++k) {
                std::swap(A[c * M + k], A[piv * M + k]);
            }
            std::swap(bx[c], bx[piv]);
            std::swap(by[c], by[piv]);
        }
        for (std::size_t r = c + 1; r < M; ++r) {
            const double f = A[r * M + c] / A[c * M + c];
            if (f == 0.0) {
                continue;
            }
            for (std::size_t k = c; k < M; ++k) {
                A[r * M + k] -= f * A[c * M + k];
            }
            bx[r] -= f * bx[c];
            by[r] -= f * by[c];
        }
    }
    ctrl.assign(M, Vec2{});
    for (std::size_t i = M; i-- > 0;) {
        double sx = bx[i];
        double sy = by[i];
        for (std::size_t k = i + 1; k < M; ++k) {
            sx -= A[i * M + k] * ctrl[k].x;
            sy -= A[i * M + k] * ctrl[k].y;
        }
        ctrl[i] = {sx / A[i * M + i], sy / A[i * M + i]};
    }
    return true;
}

/// SPLINE's toLerance: the fewest control points (from degree + 1 up) whose least-squares
/// curve, its ends on the first and last fit points, passes within `tolerance` of every fit
/// point; through all of them (interpolation) when nothing fewer does. Empty when there are
/// too few points to fit.
inline std::vector<Vec2> fit_within(std::span<const Vec2> fit, int degree_in, FitParam mode, double tolerance) {
    const int m = static_cast<int>(fit.size());
    if (m < 3 || !(tolerance > 0.0)) {
        return {};
    }
    const std::vector<double> u = fit_parameters(fit, mode);
    std::vector<double> basis;
    for (int count = std::max(2, std::min(degree_in, m - 1) + 1); count < m; ++count) {
        const int degree = effective_degree(count, degree_in);
        const std::vector<double> knots = clamped_knots(count, degree);
        const int inner = count - 2; // the control points between the two fixed ends
        const auto K = static_cast<std::size_t>(inner);
        std::vector<double> ata(K * K, 0.0);
        std::vector<double> atx(K, 0.0);
        std::vector<double> aty(K, 0.0);
        for (int k = 1; k < m - 1; ++k) {
            const double t = u[static_cast<std::size_t>(k)];
            const int span = find_span(count - 1, degree, t, knots);
            basis_functions(span, t, degree, knots, basis);
            std::vector<double> row(static_cast<std::size_t>(count), 0.0);
            for (int j = 0; j <= degree; ++j) {
                row[static_cast<std::size_t>(span - degree + j)] = basis[static_cast<std::size_t>(j)];
            }
            const Vec2 r = fit[static_cast<std::size_t>(k)] - fit.front() * row.front() - fit.back() * row.back();
            for (std::size_t i = 0; i < K; ++i) {
                const double ni = row[i + 1];
                if (ni == 0.0) {
                    continue;
                }
                atx[i] += ni * r.x;
                aty[i] += ni * r.y;
                for (std::size_t j = 0; j < K; ++j) {
                    ata[i * K + j] += ni * row[j + 1];
                }
            }
        }
        // Gaussian elimination with partial pivoting on the normal equations.
        bool singular = false;
        for (std::size_t c = 0; c < K && !singular; ++c) {
            std::size_t piv = c;
            for (std::size_t r = c + 1; r < K; ++r) {
                if (std::abs(ata[r * K + c]) > std::abs(ata[piv * K + c])) {
                    piv = r;
                }
            }
            if (std::abs(ata[piv * K + c]) < 1e-14) {
                singular = true;
                break;
            }
            if (piv != c) {
                for (std::size_t k = 0; k < K; ++k) {
                    std::swap(ata[c * K + k], ata[piv * K + k]);
                }
                std::swap(atx[c], atx[piv]);
                std::swap(aty[c], aty[piv]);
            }
            for (std::size_t r = c + 1; r < K; ++r) {
                const double f = ata[r * K + c] / ata[c * K + c];
                for (std::size_t k = c; k < K; ++k) {
                    ata[r * K + k] -= f * ata[c * K + k];
                }
                atx[r] -= f * atx[c];
                aty[r] -= f * aty[c];
            }
        }
        if (singular) {
            continue;
        }
        std::vector<Vec2> ctrl(static_cast<std::size_t>(count));
        ctrl.front() = fit.front();
        ctrl.back() = fit.back();
        for (std::size_t i = K; i-- > 0;) {
            double sx = atx[i];
            double sy = aty[i];
            for (std::size_t k = i + 1; k < K; ++k) {
                sx -= ata[i * K + k] * ctrl[k + 1].x;
                sy -= ata[i * K + k] * ctrl[k + 1].y;
            }
            ctrl[i + 1] = {sx / ata[i * K + i], sy / ata[i * K + i]};
        }
        double worst = 0.0;
        for (int k = 0; k < m; ++k) {
            const double t = u[static_cast<std::size_t>(k)];
            const Vec2 c = de_boor(find_span(count - 1, degree, t, knots), t, degree, knots, ctrl);
            worst = std::max(worst, length(c - fit[static_cast<std::size_t>(k)]));
        }
        if (worst <= tolerance) {
            return ctrl;
        }
    }
    return {}; // nothing fewer will do: the caller interpolates
}

/// Interpolate with the asked parameterisation, falling back to Uniform (always
/// solvable) -- the SPLINE command's rule, so a preview and its commit agree.
inline std::vector<Vec2> fit_or_fallback(std::span<const Vec2> fit, int degree, FitParam mode,
                                         std::optional<Vec2> start_dir = std::nullopt,
                                         std::optional<Vec2> end_dir = std::nullopt) {
    std::vector<Vec2> ctrl;
    if (start_dir || end_dir) {
        if (fit_control_points_tangent(fit, degree, mode, start_dir, end_dir, ctrl) ||
            fit_control_points_tangent(fit, degree, FitParam::Uniform, start_dir, end_dir, ctrl)) {
            return ctrl;
        }
    }
    if (fit_control_points(fit, degree, mode, ctrl)) {
        return ctrl;
    }
    if (mode != FitParam::Uniform && fit_control_points(fit, degree, FitParam::Uniform, ctrl)) {
        return ctrl;
    }
    return std::vector<Vec2>(fit.begin(), fit.end());
}

/// SPLINE's Fit method -- the one rule for the command and for a fitted spline drawn again
/// from its fit points: the control points of the spline through `pts` (closed: round to the
/// first again, smooth across that seam), within `tolerance` of them when above 0 (open,
/// without end tangents), leaving and arriving along `start_dir` / `end_dir` when given.
inline std::vector<Vec2> fit_spline(std::span<const Vec2> pts, int degree, FitParam mode, double tolerance,
                                    bool closed, std::optional<Vec2> start_dir, std::optional<Vec2> end_dir) {
    std::vector<Vec2> through(pts.begin(), pts.end());
    std::optional<Vec2> seam; // closed: the same direction leaving and arriving at the start
    if (closed && !pts.empty()) {
        through.push_back(pts.front());
        if (pts.size() >= 3) {
            seam = pts[1] - pts.back();
        }
    }
    std::vector<Vec2> ctrl;
    if (tolerance > 0.0 && !closed && !start_dir && !end_dir) {
        ctrl = fit_within(through, degree, mode, tolerance);
    }
    if (ctrl.empty()) {
        ctrl = fit_or_fallback(through, degree, mode, closed ? seam : start_dir, closed ? seam : end_dir);
    }
    if (closed && ctrl.size() >= 2) {
        ctrl.back() = ctrl.front(); // closed exactly, whatever the fit's rounding
    }
    return ctrl;
}

} // namespace musacad::core::spline
