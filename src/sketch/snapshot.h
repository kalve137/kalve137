// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sketch/sketch.h"
#include "snapshot/sketch.h"

#include <vector>

namespace kalve137::sketch {

struct Placement {
    snapshot::Point origin{};
    snapshot::Point x{1.0, 0.0, 0.0};
    snapshot::Point y{0.0, 1.0, 0.0};
};

snapshot::Point placed(const Placement& placement, Vec2 p);
double curveDeflection(const Sketch& sketch);
snapshot::Sketch snapshotOf(const Sketch& sketch, const Placement& placement, int feature = 0);
std::vector<snapshot::SketchPoint> pointsOf(const Sketch& sketch, const Placement& placement);

} // namespace kalve137::sketch
