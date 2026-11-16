// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "kernel/shapes.h"

#include <string>
#include <vector>

namespace kalve137::rebuild {

struct Outline {
    std::string item;
    kernel::Loop loop;
    std::vector<std::string> sides;
};

std::vector<kernel::Point2> polygon(const kernel::Loop& loop);
double enclosedArea(const kernel::Loop& loop);
kernel::Point2 interiorPoint(const kernel::Loop& loop);
bool inside(const kernel::Loop& outer, const kernel::Loop& inner);

} // namespace kalve137::rebuild
