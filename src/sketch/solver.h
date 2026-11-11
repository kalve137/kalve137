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

SolveResult solve(Sketch& sketch);

} // namespace kalve137::sketch
