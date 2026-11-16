// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace kalve137::lang {

struct Span {
    int line = 0;
    int column = 0;
};

struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;

struct Expr {
    enum class Kind { Number, Name, Path, Call, Thread, Unary, Binary };

    Kind kind = Kind::Number;
    Span span;
    double number = 0.0;
    std::string unit;
    std::string text;
    std::vector<std::string> path;
    char op = 0;
    std::vector<ExprPtr> args;
};

struct Point {
    ExprPtr x;
    ExprPtr y;
};

struct SketchItem {
    enum class Kind { Rect, Circle, Point, Line, Arc, Constraint };

    Kind kind = Kind::Rect;
    std::string name;
    Span span;
    Point center;
    Point size;
    ExprPtr diameter;
    std::vector<std::string> refs;
    std::vector<Span> refSpans;
    ExprPtr call;
    ExprPtr value;
};

struct Statement {
    enum class Kind { Part, Param, Sketch, Extrude, Cut, Fillet, Check };

    Kind kind = Kind::Param;
    std::string name;
    Span span;
    ExprPtr value;
    ExprPtr plane;
    std::vector<SketchItem> items;
    std::string sketch;
    Span sketchSpan;
    bool down = false;
    bool through = false;
    ExprPtr lhs;
    ExprPtr rhs;
    std::string comparison;
};

struct Document {
    std::vector<Statement> statements;

    const Statement* find(const std::string& name) const;
};

struct Diagnostic {
    enum class Severity { Error, Warning, Note };

    Severity severity = Severity::Error;
    std::string code;
    Span span;
    std::string statement;
    std::string message;
    std::string hint;
};

bool hasErrors(const std::vector<Diagnostic>& diagnostics);
std::string toString(Diagnostic::Severity severity);
std::string toString(Statement::Kind kind);
std::string toText(const Diagnostic& diagnostic, const std::string& file = {});

} // namespace kalve137::lang
