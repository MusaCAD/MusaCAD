// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "musacad/core/math/math.hpp"
#include "musacad/core/text/parse_double.hpp"

// Drawing units (AutoCAD UNITS): how lengths and angles are DISPLAYED -- inquiry
// reports, the coordinate readout, dynamic input. Stored with the drawing. Dimension
// text keeps its own precision from the dimension style, as in AutoCAD (DIMLUNIT).
//
// The enumerators carry AutoCAD's LUNITS / AUNITS values.

namespace musacad::core {

enum class LinearFormat : std::uint8_t {
    Scientific = 1,    ///< 1.2346E+01
    Decimal = 2,       ///< 12.3457
    Engineering = 3,   ///< 1'-0.3457"   (drawing unit = inch)
    Architectural = 4, ///< 1'-0 3/8"    (drawing unit = inch; precision = 1/2^n)
    Fractional = 5,    ///< 12 3/8       (precision = 1/2^n)
};

enum class AngleFormat : std::uint8_t {
    DecimalDegrees = 0, ///< 45.0
    DegMinSec = 1,      ///< 45d30'0"
    Grads = 2,          ///< 50.0g
    Radians = 3,        ///< 0.7854r
    Surveyor = 4,       ///< N 45d0'0" E
};

struct DrawingUnits {
    LinearFormat linear = LinearFormat::Decimal;
    std::uint8_t linear_precision = 4; ///< 0..8 decimals, or 1/2^n for the fractional forms
    AngleFormat angular = AngleFormat::DecimalDegrees;
    std::uint8_t angular_precision = 0; ///< 0..8
    bool clockwise = false;             ///< angles measured clockwise
    double base_angle = 0.0;            ///< direction of angle 0, radians CCW from +x
    /// INSUNITS: the drawing unit blocks and images are scaled to on insertion (0
    /// unitless, 1 inches, 2 feet, 3 miles, 4 millimetres, 5 centimetres, 6 metres, 7
    /// kilometres, 8 microinches, 9 mils, 10 yards, 11 angstroms, 12 nanometres, 13
    /// microns, 14 decimetres, 15 decametres, 16 hectometres, 17 gigametres, 18
    /// astronomical units, 19 light years, 20 parsecs).
    std::uint8_t insunits = 0;
    friend bool operator==(const DrawingUnits&, const DrawingUnits&) = default;
};

namespace units {
/// A length as a user types it, in drawing units: a decimal ("12.5", "-3", "1e3"), a
/// fraction ("3/8", "12 3/8", "12-3/8"), or feet and inches ("1'-6\"", "1'6", "1'",
/// "6\"", "1'-6 1/2\"", "-2'3"), a foot being twelve drawing units as the Engineering
/// and Architectural formats print them. Nothing for anything else.
inline std::optional<double> parse_length_text(std::string_view text) {
    std::size_t b = 0;
    std::size_t e = text.size();
    while (b < e && (text[b] == ' ' || text[b] == '\t')) {
        ++b;
    }
    while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t')) {
        --e;
    }
    std::string_view t = text.substr(b, e - b);
    if (t.empty()) {
        return std::nullopt;
    }
    bool neg = false;
    if (t.front() == '-' || t.front() == '+') {
        neg = t.front() == '-';
        t.remove_prefix(1);
        if (t.empty()) {
            return std::nullopt;
        }
    }
    // A plain decimal, the whole string (the C locale's '.', whatever the application's).
    const auto decimal = [](std::string_view s, double& out) {
        if (s.empty() || s.front() == '+' || s.front() == '-') {
            return false;
        }
        return parse_double_all(s, out) && std::isfinite(out);
    };
    double v = 0.0;
    if (decimal(t, v)) {
        return neg ? -v : v;
    }
    // Feet, then inches: the foot mark splits them; a dash after it is a separator.
    double feet = 0.0;
    std::string_view in = t;
    if (const auto ft = t.find('\''); ft != std::string_view::npos) {
        if (ft == 0 || !decimal(t.substr(0, ft), feet)) {
            return std::nullopt;
        }
        in = t.substr(ft + 1);
        while (!in.empty() && (in.front() == '-' || in.front() == ' ')) {
            in.remove_prefix(1);
        }
    }
    if (!in.empty() && in.back() == '"') {
        in.remove_suffix(1);
    }
    double inches = 0.0;
    if (!in.empty()) {
        // "6", "6.5", "3/8", "6 3/8", "6-3/8".
        const auto fraction = [&](std::string_view f, double& out) {
            const auto sl = f.find('/');
            double n = 0.0;
            double d = 0.0;
            if (sl == std::string_view::npos || !decimal(f.substr(0, sl), n) || !decimal(f.substr(sl + 1), d) ||
                d <= 0.0) {
                return false;
            }
            out = n / d;
            return true;
        };
        std::size_t sep = in.find(' ');
        if (sep == std::string_view::npos) {
            sep = in.find('-');
        }
        double whole = 0.0;
        double frac = 0.0;
        if (sep != std::string_view::npos && decimal(in.substr(0, sep), whole) && fraction(in.substr(sep + 1), frac)) {
            inches = whole + frac;
        } else if (decimal(in, whole)) {
            inches = whole;
        } else if (fraction(in, frac)) {
            inches = frac;
        } else {
            return std::nullopt;
        }
    } else if (t.find('\'') == std::string_view::npos) {
        return std::nullopt; // a lone inch mark
    }
    const double total = feet * 12.0 + inches;
    return neg ? -total : total;
}

/// How many `to` units one `from` unit is (INSUNITS codes): what a block, a reference or a
/// paste from a drawing in `from` units is scaled by in a drawing in `to` units. 1 when
/// either drawing is unitless (0) or the code is unknown.
inline double insunits_factor(std::uint8_t from, std::uint8_t to) {
    static constexpr double kMetres[] = {
        0.0,                    // 0 unitless
        0.0254,                 // 1 inches
        0.3048,                 // 2 feet
        1609.344,               // 3 miles
        0.001,                  // 4 millimetres
        0.01,                   // 5 centimetres
        1.0,                    // 6 metres
        1000.0,                 // 7 kilometres
        0.0254e-6,              // 8 microinches
        0.0254e-3,              // 9 mils
        0.9144,                 // 10 yards
        1e-10,                  // 11 angstroms
        1e-9,                   // 12 nanometres
        1e-6,                   // 13 microns
        0.1,                    // 14 decimetres
        10.0,                   // 15 decametres
        100.0,                  // 16 hectometres
        1e9,                    // 17 gigametres
        1.495978707e11,         // 18 astronomical units
        9.4607304725808e15,     // 19 light years
        3.0856775814913673e16,  // 20 parsecs
    };
    constexpr std::size_t n = sizeof(kMetres) / sizeof(kMetres[0]);
    if (from == 0 || to == 0 || from == to || from >= n || to >= n) {
        return 1.0;
    }
    return kMetres[from] / kMetres[to];
}

/// The INSUNITS names, in AutoCAD's order (index = the value).
inline const char* insunits_name(std::uint8_t v) {
    static constexpr const char* kNames[] = {
        "Unitless",     "Inches",      "Feet",        "Miles",     "Millimeters", "Centimeters",
        "Meters",       "Kilometers",  "Microinches", "Mils",      "Yards",       "Angstroms",
        "Nanometers",   "Microns",     "Decimeters",  "Decameters", "Hectometers", "Gigameters",
        "Astronomical", "Light years", "Parsecs"};
    return v < 21 ? kNames[v] : "Unitless";
}
inline constexpr int kInsunitsCount = 21;
} // namespace units

namespace units {

inline std::string fraction_text(double value, int precision) {
    // Whole part + a reduced fraction with denominator 2^precision ("12 3/8"); a value
    // that rounds to a whole number is printed as such.
    const int denom = 1 << (precision < 0 ? 0 : (precision > 8 ? 8 : precision));
    const bool neg = value < 0.0;
    value = std::abs(value);
    long whole = static_cast<long>(std::floor(value));
    long numer = static_cast<long>(std::lround((value - static_cast<double>(whole)) * denom));
    long d = denom;
    if (numer == d) {
        ++whole;
        numer = 0;
    }
    while (numer > 0 && numer % 2 == 0 && d % 2 == 0) {
        numer /= 2;
        d /= 2;
    }
    std::string out = neg ? "-" : "";
    if (numer == 0) {
        out += std::to_string(whole);
    } else if (whole == 0) {
        out += std::to_string(numer) + "/" + std::to_string(d);
    } else {
        out += std::to_string(whole) + " " + std::to_string(numer) + "/" + std::to_string(d);
    }
    return out;
}

inline std::string format_length(double v, const DrawingUnits& u) {
    char buf[64];
    const int p = u.linear_precision > 8 ? 8 : u.linear_precision;
    if (std::abs(v) < 0.5 * std::pow(10.0, -p)) {
        v = 0.0; // a value that rounds to zero prints as 0, never "-0.0000"
    }
    switch (u.linear) {
    case LinearFormat::Scientific:
        std::snprintf(buf, sizeof(buf), "%.*E", p, v);
        return buf;
    case LinearFormat::Engineering: {
        const bool neg = v < 0.0;
        const double in = std::abs(v);
        const long ft = static_cast<long>(std::floor(in / 12.0));
        const double rem = in - static_cast<double>(ft) * 12.0;
        std::snprintf(buf, sizeof(buf), "%s%ld'-%.*f\"", neg ? "-" : "", ft, p, rem);
        return buf;
    }
    case LinearFormat::Architectural: {
        const bool neg = v < 0.0;
        const double in = std::abs(v);
        long ft = static_cast<long>(std::floor(in / 12.0));
        double rem = in - static_cast<double>(ft) * 12.0;
        std::string fr = fraction_text(rem, p);
        if (fr == "12") { // the fraction rounded up to a whole foot
            ++ft;
            fr = "0";
        }
        return std::string(neg ? "-" : "") + std::to_string(ft) + "'-" + fr + "\"";
    }
    case LinearFormat::Fractional:
        return fraction_text(v, p);
    case LinearFormat::Decimal:
        break;
    }
    std::snprintf(buf, sizeof(buf), "%.*f", p, v);
    return buf;
}

/// Degrees/minutes/seconds of a non-negative angle in degrees, with `p` decimals on
/// the seconds (AutoCAD: precision 0 -> "45d", 1..2 -> minutes, 3+ -> seconds).
inline std::string dms_text(double deg, int p) {
    char buf[64];
    if (p == 0) {
        std::snprintf(buf, sizeof(buf), "%.0fd", deg);
        return buf;
    }
    const double dd = std::floor(deg);
    const double mm_f = (deg - dd) * 60.0;
    if (p <= 2) {
        std::snprintf(buf, sizeof(buf), "%.0fd%.0f'", dd, mm_f);
        return buf;
    }
    const double mm = std::floor(mm_f);
    const double ss = (mm_f - mm) * 60.0;
    std::snprintf(buf, sizeof(buf), "%.0fd%.0f'%.*f\"", dd, mm, p - 3 > 0 ? p - 3 : 0, ss);
    return buf;
}

/// An angle in radians (mathematical, CCW from +x) displayed per the drawing units:
/// measured from the base direction, clockwise if asked, in the chosen format.
inline std::string format_angle(double radians, const DrawingUnits& u) {
    double a = radians - u.base_angle;
    if (u.clockwise) {
        a = -a;
    }
    a = std::fmod(a, kTwoPi);
    if (a < 0.0) {
        a += kTwoPi;
    }
    if (std::abs(a) < 1e-12 || std::abs(a - kTwoPi) < 1e-12) {
        a = 0.0; // never "-0", and a full turn reads as 0
    }
    const int p = u.angular_precision > 8 ? 8 : u.angular_precision;
    char buf[64];
    switch (u.angular) {
    case AngleFormat::DegMinSec:
        return dms_text(to_degrees(a), p);
    case AngleFormat::Grads:
        std::snprintf(buf, sizeof(buf), "%.*fg", p, a * (200.0 / kPi));
        return buf;
    case AngleFormat::Radians:
        std::snprintf(buf, sizeof(buf), "%.*fr", p, a);
        return buf;
    case AngleFormat::Surveyor: {
        // Bearing from north or south toward east or west, e.g. N 45d E.
        const double deg = to_degrees(a); // from east, CCW
        const bool north = deg <= 180.0;
        const bool east = deg <= 90.0 || deg >= 270.0;
        double from_ns = north ? std::abs(90.0 - deg) : std::abs(270.0 - deg);
        if (from_ns > 90.0) {
            from_ns = 180.0 - from_ns;
        }
        return std::string(north ? "N " : "S ") + dms_text(from_ns, p) + (east ? " E" : " W");
    }
    case AngleFormat::DecimalDegrees:
        break;
    }
    std::snprintf(buf, sizeof(buf), "%.*f", p, to_degrees(a));
    return buf;
}

inline std::string format_point(Vec2 p, const DrawingUnits& u) {
    return format_length(p.x, u) + ", " + format_length(p.y, u);
}

inline const char* linear_name(LinearFormat f) {
    switch (f) {
    case LinearFormat::Scientific:
        return "Scientific";
    case LinearFormat::Engineering:
        return "Engineering";
    case LinearFormat::Architectural:
        return "Architectural";
    case LinearFormat::Fractional:
        return "Fractional";
    case LinearFormat::Decimal:
        break;
    }
    return "Decimal";
}

inline const char* angular_name(AngleFormat f) {
    switch (f) {
    case AngleFormat::DegMinSec:
        return "Deg/Min/Sec";
    case AngleFormat::Grads:
        return "Grads";
    case AngleFormat::Radians:
        return "Radians";
    case AngleFormat::Surveyor:
        return "Surveyor";
    case AngleFormat::DecimalDegrees:
        break;
    }
    return "Decimal degrees";
}

} // namespace units
} // namespace musacad::core
