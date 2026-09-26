// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace musacad::command::calc {

/// A CAL / QUICKCALC value: a number, or a point / vector `[x,y]`.
struct Value {
    double x = 0.0;
    double y = 0.0;
    bool vector = false;
};

/// Evaluate AutoCAD's calculator expressions: + - * / ^ and parentheses; `[x,y]`
/// points and vectors (added, subtracted, scaled, dotted); the functions sin, cos, tan
/// (degrees in), asin, acos, atan (degrees out), sqrt, sqr, abs (a vector's length),
/// ln, log, exp, exp10, round, trunc, floor, ceil, r2d, d2r, dist(p,q), ang(p,q)
/// (degrees), vec(p,q), vec1(p,q) (unit), nor(p,q) (the left normal), rad(p) (|p|),
/// cvunit(v,from,to) for inch, foot, yard, mile, mm, cm, m, km; the constants pi and e.
/// Returns nullopt with `error` set for anything else.
[[nodiscard]] std::optional<Value> evaluate(std::string_view expression, std::string& error);

/// "12.5" or "[3,4]" with up to `decimals` places, trailing zeros trimmed.
[[nodiscard]] std::string format(const Value& v, int decimals = 8);

} // namespace musacad::command::calc
