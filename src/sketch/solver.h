// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sketch/sketch.h"

#include <set>
#include <vector>

namespace kalve137::sketch {

struct SolveResult {
    bool solved = false;
    int dof = -1;
    std::vector<Id> conflicting;
    std::vector<Id> redundant;
    std::set<Id> fullyConstrained;

    bool isFullyConstrained() const { return solved && dof == 0 && conflicting.empty(); }
};

struct DragTarget {
    Id point = 0;
    Vec2 pos;
};

SolveResult solve(Sketch& sketch, const std::vector<DragTarget>& targets = {});
SolveResult solveMoved(Sketch& sketch, const Sketch& before);

} // namespace kalve137::sketch
