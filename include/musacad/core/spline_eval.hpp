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
// Musa splines are clamped B-splines with uniform interior knots on [0,1]; `degree` is
// lowered to count-1 when there are too few control points, as AutoCAD does.

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

/// The curve point at t in [0,1].
inline Vec2 evaluate(std::span<const Vec2> ctrl, int degree_in, double t) {
    const int count = static_cast<int>(ctrl.size());
    if (count == 0) {
        return {};
    }
    if (count == 1) {
        return ctrl[0];
    }
    const int degree = effective_degree(count, degree_in);
    const std::vector<double> knots = clamped_knots(count, degree);
    t = std::clamp(t, 0.0, 1.0);
    return de_boor(find_span(count - 1, degree, t, knots), t, degree, knots, ctrl);
}

/// Chords along the curve: 16 samples per span, bounded -- the rule every consumer draws
/// and picks with.
inline void tessellate(std::span<const Vec2> ctrl, std::uint32_t degree_in, std::vector<Vec2>& out) {
    const int count = static_cast<int>(ctrl.size());
    if (count <= 0) {
        return;
    }
    if (count == 1) {
        out.push_back(ctrl[0]);
        return;
    }
    const int degree = effective_degree(count, static_cast<int>(degree_in));
    const std::vector<double> knots = clamped_knots(count, degree);
    const int n = count - 1;
    const auto samples = std::clamp<std::size_t>(static_cast<std::size_t>(count - 1) * 16,
                                                 std::size_t{2}, std::size_t{8192});
    out.reserve(out.size() + samples + 1);
    for (std::size_t i = 0; i <= samples; ++i) {
        const double t = std::clamp(static_cast<double>(i) / static_cast<double>(samples), 0.0, 1.0);
        out.push_back(de_boor(find_span(n, degree, t, knots), t, degree, knots, ctrl));
    }
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

} // namespace musacad::core::spline
