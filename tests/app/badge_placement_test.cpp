// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/badge_placement.h"

#include "sketch/sketch.h"

#include <gtest/gtest.h>

#include <QString>

#include <algorithm>
#include <vector>

namespace {

using kalve137::app::BadgePlace;
using kalve137::app::placeBadges;
using kalve137::sketch::ConstraintKind;
using kalve137::sketch::Id;
using kalve137::sketch::Sketch;

int countOf(const std::vector<BadgePlace>& badges, Id constraint, const char* icon)
{
    return static_cast<int>(std::ranges::count_if(badges, [&](const BadgePlace& badge) {
        return badge.constraint == constraint && badge.icon == QLatin1StringView(icon);
    }));
}

TEST(BadgePlacement, ACoincidenceOfTwoCentersShowsAConcentricBadgeOnEachRing)
{
    Sketch s;
    const Id inner = s.addPoint({0.0, 0.0});
    const Id outer = s.addPoint({1.0, 1.0});
    s.addCircle(inner, 5.0);
    s.addArc(outer, s.addPoint({11.0, 1.0}), s.addPoint({1.0, 11.0}));
    const Id concentric = s.addConstraint(ConstraintKind::Coincident, {inner, outer});
    const Id a = s.addPoint({20.0, 0.0});
    const Id b = s.addPoint({30.0, 0.0});
    s.addLine(a, s.addPoint({25.0, 5.0}));
    const Id joined = s.addConstraint(ConstraintKind::Coincident, {a, b});
    const std::vector<BadgePlace> badges = placeBadges(s);
    EXPECT_EQ(countOf(badges, concentric, "concentric"), 2);
    EXPECT_EQ(countOf(badges, joined, "concentric"), 0);
    EXPECT_TRUE(std::ranges::none_of(badges, [&](const BadgePlace& badge) { return badge.constraint == joined; }));
}

} // namespace
