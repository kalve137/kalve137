// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <vector>

namespace kalve137::solve {

struct Point {
    double u = 0.0;
    double v = 0.0;
};

struct Line {
    int a = -1;
    int b = -1;
};

struct Arc {
    int center = -1;
    int start = -1;
    int end = -1;
};

struct Circle {
    int center = -1;
    double radius = 0.0;
};

struct Ref {
    enum class Kind { Point, Line, Arc, Circle };

    Kind kind = Kind::Point;
    int index = -1;
};

struct Constraint {
    enum class Kind {
        X,
        Y,
        Coincident,
        Horizontal,
        Vertical,
        Parallel,
        Perpendicular,
        Equal,
        Tangent,
        Distance,
        Radius,
        Angle
    };

    Kind kind = Kind::Coincident;
    std::vector<Ref> refs;
    double value = 0.0;
};

struct Sketch {
    std::vector<Point> points;
    std::vector<Line> lines;
    std::vector<Arc> arcs;
    std::vector<Circle> circles;
    std::vector<Constraint> constraints;
};

struct Solution {
    enum class Status { Solved, Conflicting, Redundant, Failed };

    Status status = Status::Failed;
    std::vector<Point> points;
    std::vector<double> circleRadii;
    std::vector<double> arcRadii;
    int freedom = 0;
    std::vector<int> freePoints;
    std::vector<int> freeCircles;
    std::vector<int> conflicting;
    std::vector<int> redundant;
};

Solution solve(const Sketch& sketch);

std::optional<std::vector<Point>> positions(const Sketch& sketch);

} // namespace kalve137::solve
