// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sketch/sketch.h"

#include <QString>

#include <vector>

namespace kalve137::app {

struct BadgePlace {
    sketch::Id constraint = 0;
    QString icon;
    sketch::Vec2 at;
    sketch::Vec2 side;
    double pixels = 0.0;

    sketch::Vec2 center(double worldPerPixel) const { return at + side * (worldPerPixel * pixels); }
};

std::vector<BadgePlace> placeBadges(const sketch::Sketch& sketch);

} // namespace kalve137::app
