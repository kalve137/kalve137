// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <optional>

namespace kalve137::geometry {

struct Measurement {
    std::optional<double> length;
    std::optional<double> area;
    std::optional<double> radius;
    std::optional<gp_Pnt> position;

    std::optional<double> distance;
    gp_Pnt from;
    gp_Pnt to;
    std::optional<double> angle;
};

Measurement measure(const TopoDS_Shape& shape);
Measurement measure(const TopoDS_Shape& a, const TopoDS_Shape& b);

struct BodyProperties {
    double volume = 0.0;
    double area = 0.0;
    gp_Pnt centerOfMass;
    gp_Pnt boxMin;
    gp_Pnt boxMax;
};

BodyProperties properties(const TopoDS_Shape& shape);

} // namespace kalve137::geometry
