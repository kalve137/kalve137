// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <TopoDS_Shape.hxx>

#include <numbers>

namespace kalve137::geometry {

inline constexpr double kDisplayDeviationCoefficient = 0.001;
inline constexpr double kDisplayDeviationAngle = 20.0 * std::numbers::pi / 180.0;

double displayDeflection(const TopoDS_Shape& shape);

void ensureMesh(const TopoDS_Shape& shape);

} // namespace kalve137::geometry
