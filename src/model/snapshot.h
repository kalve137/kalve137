// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lang/ast.h"
#include "lang/check.h"
#include "model/vec.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace kalve137::model {

struct Face {
    std::string selector;
    std::string name;
    std::string madeBy;
    std::string kind;
    double area = 0.0;
    Vec3 centroid;
    Vec3 normal;
    Vec3 axis;
    double radius = 0.0;
    std::vector<Vec3> vertices;
    std::vector<std::array<int, 3>> triangles;
};

struct Edge {
    std::string selector;
    int faceA = -1;
    int faceB = -1;
    std::string kind;
    double length = 0.0;
    std::vector<Vec3> points;
};

struct CheckResult {
    std::string name;
    lang::Span span;
    bool passed = false;
    lang::Quantity lhs;
    lang::Quantity rhs;
    std::string comparison;
    std::string expression;
};

struct SolvedPoint {
    std::string name;
    double u = 0.0;
    double v = 0.0;
    bool free = false;
};

struct SolvedLine {
    std::string name;
    std::string from;
    std::string to;
    double length = 0.0;
};

struct SolvedSketch {
    std::string name;
    int freedom = 0;
    std::vector<SolvedPoint> points;
    std::vector<SolvedLine> lines;
};

struct SketchPlacement {
    std::string sketch;
    std::string on;
    Frame frame;
};

struct Timing {
    std::string statement;
    double milliseconds = 0.0;
    double inspectMilliseconds = 0.0;
    double nameMilliseconds = 0.0;
};

struct Snapshot {
    std::string part;
    std::string at;
    bool built = false;
    bool complete = false;
    std::vector<Face> faces;
    std::vector<Edge> edges;
    double volume = 0.0;
    Vec3 boxMin;
    Vec3 boxMax;
    std::map<std::string, lang::Quantity> params;
    std::vector<CheckResult> checks;
    std::vector<SolvedSketch> sketches;
    std::vector<SketchPlacement> placements;
    std::vector<lang::Diagnostic> diagnostics;
    std::vector<Timing> timings;
    double totalMilliseconds = 0.0;

    int faceIndex(const std::string& selector) const;
};

} // namespace kalve137::model
