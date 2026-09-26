// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/command/calc.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <vector>

namespace musacad::command::calc {

namespace {
struct Parser {
    std::string_view s;
    std::size_t i = 0;
    std::string& err;
    bool failed = false;

    Parser(std::string_view text, std::string& e) : s(text), err(e) {}

    void fail(const std::string& m) {
        if (!failed) {
            err = m;
            failed = true;
        }
    }
    void skip() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) {
            ++i;
        }
    }
    bool peek(char c) {
        skip();
        return i < s.size() && s[i] == c;
    }
    bool take(char c) {
        if (peek(c)) {
            ++i;
            return true;
        }
        return false;
    }
    static Value num(double v) { return Value{v, 0.0, false}; }

    Value expr() {
        Value v = term();
        while (!failed) {
            if (take('+')) {
                v = add(v, term(), 1.0);
            } else if (take('-')) {
                v = add(v, term(), -1.0);
            } else {
                break;
            }
        }
        return v;
    }
    Value add(Value a, Value b, double sign) {
        if (a.vector != b.vector) {
            fail("cannot add a number and a point");
            return {};
        }
        return Value{a.x + sign * b.x, a.y + sign * b.y, a.vector};
    }
    Value term() {
        Value v = unary();
        while (!failed) {
            if (take('*')) {
                const Value r = unary();
                if (v.vector && r.vector) {
                    v = num(v.x * r.x + v.y * r.y); // dot product
                } else if (v.vector) {
                    v = Value{v.x * r.x, v.y * r.x, true};
                } else if (r.vector) {
                    v = Value{r.x * v.x, r.y * v.x, true};
                } else {
                    v = num(v.x * r.x);
                }
            } else if (take('/')) {
                const Value r = unary();
                if (r.vector || r.x == 0.0) {
                    fail(r.vector ? "cannot divide by a point" : "division by zero");
                    return {};
                }
                v = Value{v.x / r.x, v.y / r.x, v.vector};
            } else {
                break;
            }
        }
        return v;
    }
    Value unary() {
        if (take('-')) {
            const Value v = unary();
            return Value{-v.x, -v.y, v.vector};
        }
        if (take('+')) {
            return unary();
        }
        return power();
    }
    Value power() {
        const Value v = primary();
        if (take('^')) {
            const Value e = unary();
            if (v.vector || e.vector) {
                fail("a power needs numbers");
                return {};
            }
            return num(std::pow(v.x, e.x));
        }
        return v;
    }
    Value primary() {
        skip();
        if (i >= s.size()) {
            fail("unexpected end of expression");
            return {};
        }
        const char c = s[i];
        if (c == '(') {
            ++i;
            const Value v = expr();
            if (!take(')')) {
                fail("missing )");
            }
            return v;
        }
        if (c == '[') {
            ++i;
            const Value x = expr();
            if (!take(',')) {
                fail("a point is [x,y]");
                return {};
            }
            const Value y = expr();
            if (take(',')) {
                (void)expr(); // a z is accepted and ignored (2D)
            }
            if (!take(']') || x.vector || y.vector) {
                fail("a point is [x,y]");
                return {};
            }
            return Value{x.x, y.x, true};
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.') {
            const std::size_t start = i;
            while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) != 0 || s[i] == '.')) {
                ++i;
            }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                std::size_t j = i + 1;
                if (j < s.size() && (s[j] == '+' || s[j] == '-')) {
                    ++j;
                }
                if (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])) != 0) {
                    i = j;
                    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
                        ++i;
                    }
                }
            }
            return num(std::strtod(std::string(s.substr(start, i - start)).c_str(), nullptr));
        }
        if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
            const std::size_t start = i;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) != 0 || s[i] == '_')) {
                ++i;
            }
            std::string name(s.substr(start, i - start));
            for (char& ch : name) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (name == "pi") {
                return num(std::numbers::pi);
            }
            if (name == "e") {
                return num(std::numbers::e);
            }
            if (!take('(')) {
                fail("unknown name: " + name);
                return {};
            }
            std::vector<Value> args;
            std::vector<std::string> words; // cvunit's unit names
            if (!peek(')')) {
                do {
                    skip();
                    if (name == "cvunit" && !args.empty() && i < s.size() &&
                        std::isalpha(static_cast<unsigned char>(s[i])) != 0) {
                        const std::size_t ws = i;
                        while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i])) != 0) {
                            ++i;
                        }
                        std::string w(s.substr(ws, i - ws));
                        for (char& ch : w) {
                            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                        }
                        words.push_back(w);
                        args.push_back(num(0.0));
                    } else {
                        args.push_back(expr());
                    }
                } while (!failed && take(','));
            }
            if (!take(')')) {
                fail("missing ) after " + name);
                return {};
            }
            return call(name, args, words);
        }
        fail(std::string("unexpected '") + c + "'");
        return {};
    }
    static double unit_in_metres(const std::string& u, bool& ok) {
        ok = true;
        if (u == "mm" || u == "millimeter" || u == "millimeters") {
            return 0.001;
        }
        if (u == "cm" || u == "centimeter" || u == "centimeters") {
            return 0.01;
        }
        if (u == "m" || u == "meter" || u == "meters") {
            return 1.0;
        }
        if (u == "km" || u == "kilometer" || u == "kilometers") {
            return 1000.0;
        }
        if (u == "inch" || u == "inches" || u == "in") {
            return 0.0254;
        }
        if (u == "foot" || u == "feet" || u == "ft") {
            return 0.3048;
        }
        if (u == "yard" || u == "yards" || u == "yd") {
            return 0.9144;
        }
        if (u == "mile" || u == "miles" || u == "mi") {
            return 1609.344;
        }
        ok = false;
        return 1.0;
    }
    Value call(const std::string& name, const std::vector<Value>& a, const std::vector<std::string>& words) {
        const auto need = [&](std::size_t n) {
            if (a.size() != n) {
                fail(name + " takes " + std::to_string(n) + (n == 1 ? " argument" : " arguments"));
                return false;
            }
            return true;
        };
        const auto scalar = [&](std::size_t k) {
            if (a[k].vector) {
                fail(name + " takes a number");
                return 0.0;
            }
            return a[k].x;
        };
        const auto point = [&](std::size_t k) {
            if (!a[k].vector) {
                fail(name + " takes a point");
            }
            return a[k];
        };
        const double d2r = std::numbers::pi / 180.0;
        if (name == "sin" && need(1)) {
            return num(std::sin(scalar(0) * d2r));
        }
        if (name == "cos" && need(1)) {
            return num(std::cos(scalar(0) * d2r));
        }
        if (name == "tan" && need(1)) {
            return num(std::tan(scalar(0) * d2r));
        }
        if (name == "asin" && need(1)) {
            return num(std::asin(scalar(0)) / d2r);
        }
        if (name == "acos" && need(1)) {
            return num(std::acos(scalar(0)) / d2r);
        }
        if (name == "atan" && need(1)) {
            return num(std::atan(scalar(0)) / d2r);
        }
        if (name == "sqrt" && need(1)) {
            return num(std::sqrt(scalar(0)));
        }
        if (name == "sqr" && need(1)) {
            const double v = scalar(0);
            return num(v * v);
        }
        if (name == "ln" && need(1)) {
            return num(std::log(scalar(0)));
        }
        if (name == "log" && need(1)) {
            return num(std::log10(scalar(0)));
        }
        if (name == "exp" && need(1)) {
            return num(std::exp(scalar(0)));
        }
        if (name == "exp10" && need(1)) {
            return num(std::pow(10.0, scalar(0)));
        }
        if (name == "round" && need(1)) {
            return num(std::round(scalar(0)));
        }
        if (name == "trunc" && need(1)) {
            return num(std::trunc(scalar(0)));
        }
        if (name == "floor" && need(1)) {
            return num(std::floor(scalar(0)));
        }
        if (name == "ceil" && need(1)) {
            return num(std::ceil(scalar(0)));
        }
        if (name == "r2d" && need(1)) {
            return num(scalar(0) / d2r);
        }
        if (name == "d2r" && need(1)) {
            return num(scalar(0) * d2r);
        }
        if (name == "abs" && need(1)) {
            return a[0].vector ? num(std::hypot(a[0].x, a[0].y)) : num(std::abs(a[0].x));
        }
        if (name == "rad" && need(1)) {
            const Value p = point(0);
            return num(std::hypot(p.x, p.y));
        }
        if (name == "dist" && need(2)) {
            const Value p = point(0);
            const Value q = point(1);
            return num(std::hypot(q.x - p.x, q.y - p.y));
        }
        if (name == "ang" && need(2)) {
            const Value p = point(0);
            const Value q = point(1);
            double deg = std::atan2(q.y - p.y, q.x - p.x) / d2r;
            if (deg < 0.0) {
                deg += 360.0;
            }
            return num(deg);
        }
        if ((name == "vec" || name == "vec1" || name == "nor") && need(2)) {
            const Value p = point(0);
            const Value q = point(1);
            Value v{q.x - p.x, q.y - p.y, true};
            if (name != "vec") {
                const double len = std::hypot(v.x, v.y);
                if (len < 1e-300) {
                    fail("the points coincide");
                    return {};
                }
                v.x /= len;
                v.y /= len;
                if (name == "nor") {
                    v = Value{-v.y, v.x, true};
                }
            }
            return v;
        }
        if (name == "cvunit") {
            if (a.size() != 3 || words.size() != 2 || a[0].vector) {
                fail("cvunit(value, from, to), e.g. cvunit(1, inch, mm)");
                return {};
            }
            bool ok1 = false;
            bool ok2 = false;
            const double from = unit_in_metres(words[0], ok1);
            const double to = unit_in_metres(words[1], ok2);
            if (!ok1 || !ok2) {
                fail("units: inch, foot, yard, mile, mm, cm, m, km");
                return {};
            }
            return num(a[0].x * from / to);
        }
        if (!failed) {
            fail("unknown function: " + name);
        }
        return {};
    }
};
} // namespace

std::optional<Value> evaluate(std::string_view expression, std::string& error) {
    error.clear();
    Parser p(expression, error);
    p.skip();
    if (p.i >= p.s.size()) {
        error = "empty expression";
        return std::nullopt;
    }
    const Value v = p.expr();
    p.skip();
    if (!p.failed && p.i < p.s.size()) {
        p.fail(std::string("unexpected '") + p.s[p.i] + "'");
    }
    if (p.failed || !std::isfinite(v.x) || !std::isfinite(v.y)) {
        if (!p.failed) {
            error = "the result is not a number";
        }
        return std::nullopt;
    }
    return v;
}

std::string format(const Value& v, int decimals) {
    const auto one = [decimals](double d) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, d);
        std::string s(buf);
        if (s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0') {
                s.pop_back();
            }
            if (!s.empty() && s.back() == '.') {
                s.pop_back();
            }
        }
        if (s == "-0") {
            s = "0";
        }
        return s;
    };
    return v.vector ? "[" + one(v.x) + "," + one(v.y) + "]" : one(v.x);
}

} // namespace musacad::command::calc
