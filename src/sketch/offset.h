// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sketch/sketch.h"

#include <optional>
#include <vector>

namespace kalve137::sketch {

struct Chain {
    std::vector<Id> curves;
    std::vector<bool> reversed;
    bool closed = false;
};

struct Offset {
    std::vector<Id> curves;
    Id constraint = 0;
};

std::optional<Chain> orderChain(const Sketch& sketch, const std::vector<Id>& curves);
std::vector<Id> chainThrough(const Sketch& sketch, Id curve);
int offsetSide(const Sketch& sketch, const Chain& chain, Vec2 cursor);
std::optional<Offset> addOffset(Sketch& sketch, const std::vector<Id>& curves, double amount, bool linked = true);

} // namespace kalve137::sketch
