// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lang/format.h"

#include <array>
#include <charconv>
#include <cmath>

namespace kalve137::lang {

namespace {

int precedence(const Expr& expr)
{
    if (expr.kind == Expr::Kind::Binary) {
        return expr.op == '+' || expr.op == '-' ? 1 : 2;
    }
    if (expr.kind == Expr::Kind::Unary) {
        return 3;
    }
    return 4;
}

std::string wrap(const Expr& child, bool parens)
{
    const std::string text = format(child);
    return parens ? "(" + text + ")" : text;
}

std::string point(const Point& p)
{
    return "(" + format(*p.x) + ", " + format(*p.y) + ")";
}

std::string statement(const Statement& s)
{
    switch (s.kind) {
    case Statement::Kind::Part:
        return "part " + s.name + "\n";
    case Statement::Kind::Param:
        return "param " + s.name + " = " + format(*s.value) + "\n";
    case Statement::Kind::Sketch: {
        std::string text = "sketch " + s.name + " on " + format(*s.plane) + " {\n";
        for (const SketchItem& item : s.items) {
            text += "  " + format(item) + "\n";
        }
        return text + "}\n";
    }
    case Statement::Kind::Extrude:
        return "extrude " + s.name + " = " + s.sketch + (s.down ? " down " : " up ") + format(*s.value) + "\n";
    case Statement::Kind::Cut:
        return "cut " + s.name + " = " + s.sketch + (s.through ? " through" : " depth " + format(*s.value)) + "\n";
    case Statement::Kind::Fillet:
        return "fillet " + s.name + " = " + format(*s.lhs) + " radius " + format(*s.value) + "\n";
    case Statement::Kind::Check:
        return "check " + s.name + ": " + format(*s.lhs) + " " + s.comparison + " " + format(*s.rhs) + "\n";
    }
    return {};
}

} // namespace

std::string formatNumber(double value)
{
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), result.ptr);
}

std::string formatLength(double millimeters)
{
    const double rounded = std::round(millimeters * 1e6) / 1e6;
    return formatNumber(rounded == 0.0 ? 0.0 : rounded) + " mm";
}

std::string format(const Expr& expr)
{
    switch (expr.kind) {
    case Expr::Kind::Number:
        return formatNumber(expr.number) + (expr.unit.empty() ? "" : " " + expr.unit);
    case Expr::Kind::Name:
    case Expr::Kind::Thread:
        return expr.text;
    case Expr::Kind::Path: {
        std::string text;
        for (const std::string& segment : expr.path) {
            text += (text.empty() ? "" : ".") + segment;
        }
        return text;
    }
    case Expr::Kind::Call: {
        std::string text = expr.text + "(";
        for (std::size_t i = 0; i < expr.args.size(); ++i) {
            text += (i == 0 ? "" : ", ") + format(*expr.args[i]);
        }
        return text + ")";
    }
    case Expr::Kind::Unary:
        return "-" + wrap(*expr.args[0], precedence(*expr.args[0]) < 3);
    case Expr::Kind::Binary: {
        const int own = precedence(expr);
        const std::string left = wrap(*expr.args[0], precedence(*expr.args[0]) < own);
        const std::string right = wrap(*expr.args[1], precedence(*expr.args[1]) <= own);
        return left + " " + expr.op + " " + right;
    }
    }
    return {};
}

std::string format(const SketchItem& item)
{
    switch (item.kind) {
    case SketchItem::Kind::Rect:
        return "rect " + item.name + " center " + point(item.center) + " size " + point(item.size);
    case SketchItem::Kind::Circle:
        return "circle " + item.name + " center " + (item.refs.empty() ? point(item.center) : item.refs[0]) +
               " diameter " + format(*item.diameter);
    case SketchItem::Kind::Point:
        return "point " + item.name + (item.center.x ? " near " + point(item.center) : "");
    case SketchItem::Kind::Line:
        return "line " + item.name + " from " + item.refs.at(0) + " to " + item.refs.at(1);
    case SketchItem::Kind::Arc:
        return "arc " + item.name + " from " + item.refs.at(0) + " to " + item.refs.at(1) + " center " +
               item.refs.at(2);
    case SketchItem::Kind::Constraint:
        if (item.center.x) {
            return format(*item.call) + " = " + point(item.center);
        }
        return format(*item.call) + (item.value ? " = " + format(*item.value) : "");
    }
    return {};
}

std::string format(const Document& document)
{
    std::string text;
    const Statement* previous = nullptr;
    for (const Statement& s : document.statements) {
        if (previous != nullptr &&
            (s.kind != previous->kind || s.kind == Statement::Kind::Sketch || s.kind == Statement::Kind::Part)) {
            text += "\n";
        }
        text += statement(s);
        previous = &s;
    }
    return text;
}

} // namespace kalve137::lang
