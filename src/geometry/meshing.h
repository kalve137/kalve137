// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/chord.h"

#include <TopoDS_Shape.hxx>

namespace kalve137::geometry {

inline constexpr double kDisplayDeviationCoefficient = snapshot::kDeviationCoefficient;
inline constexpr double kDisplayDeviationAngle = snapshot::kDeviationAngle;

double displayDeflection(const TopoDS_Shape& shape);

void ensureMesh(const TopoDS_Shape& shape);

} // namespace kalve137::geometry
