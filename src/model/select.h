// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lang/ast.h"
#include "lang/check.h"
#include "model/snapshot.h"

#include <string>
#include <vector>

namespace kalve137::model {

struct Selection {
    bool edges = false;
    std::vector<int> items;
};

Selection select(const lang::Expr& expr, const Snapshot& snapshot);

Selection selectUse(const lang::Expr& expr, const Snapshot& snapshot, const std::string& purpose);
int selectOneFace(const lang::Expr& expr, const Snapshot& snapshot, const std::string& purpose);

std::string selectorFor(const Snapshot& snapshot, int face);

std::string describe(const Snapshot& snapshot);
std::string describeAxes(const Frame& frame);
std::string describeFace(const Snapshot& snapshot, int face);

void assignSelectors(Snapshot& snapshot);

} // namespace kalve137::model
