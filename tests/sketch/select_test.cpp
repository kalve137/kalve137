// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/select.h"
#include "sketch/sketch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace kalve137::sketch;

namespace {

Vec2 same(Vec2 p)
{
    return p;
}

bool has(const std::vector<Id>& ids, Id id)
{
    return std::ranges::find(ids, id) != ids.end();
}

struct Layout {
    Sketch sketch;
    Id inside = 0;
    Id straddling = 0;
    Id circle = 0;
    Id arc = 0;
    Id insideStart = 0;
};

Layout layout()
{
    Layout l;
    Sketch& s = l.sketch;
    l.insideStart = s.addPoint({1, 1});
    l.inside = s.addLine(l.insideStart, s.addPoint({4, 1}));
    l.straddling = s.addLine(s.addPoint({2, 2}), s.addPoint({20, 2}));
    l.circle = s.addCircle(s.addPoint({5, 5}), 2.0);
    l.arc = s.addArc(s.addPoint({30, 30}), s.addPoint({32, 30}), s.addPoint({30, 32}));
    return l;
}

} // namespace

TEST(BoxSelect, WindowTakesOnlyWhatIsFullyInside)
{
    const Layout l = layout();
    const std::vector<Id> ids = inBox(l.sketch, same, {0, 0}, {10, 10}, BoxMode::Window);
    EXPECT_TRUE(has(ids, l.inside));
    EXPECT_TRUE(has(ids, l.circle));
    EXPECT_TRUE(has(ids, l.insideStart));
    EXPECT_FALSE(has(ids, l.straddling));
    EXPECT_FALSE(has(ids, l.arc));
    EXPECT_FALSE(has(ids, kOriginId));
}

TEST(BoxSelect, CrossingAlsoTakesWhatTouchesTheBox)
{
    const Layout l = layout();
    const std::vector<Id> ids = inBox(l.sketch, same, {10, 10}, {0, 0}, BoxMode::Crossing);
    EXPECT_TRUE(has(ids, l.inside));
    EXPECT_TRUE(has(ids, l.straddling));
    EXPECT_TRUE(has(ids, l.circle));
    EXPECT_FALSE(has(ids, l.arc));
}

TEST(BoxSelect, CrossingCatchesALinePassingThroughWithBothEndsOutside)
{
    Sketch s;
    const Id line = s.addLine(s.addPoint({-10, 5}), s.addPoint({20, 5}));
    EXPECT_TRUE(has(inBox(s, same, {0, 0}, {10, 10}, BoxMode::Crossing), line));
    EXPECT_FALSE(has(inBox(s, same, {0, 0}, {10, 10}, BoxMode::Window), line));
    EXPECT_FALSE(has(inBox(s, same, {0, 6}, {10, 10}, BoxMode::Crossing), line));
}

TEST(BoxSelect, CrossingCatchesACircleRunningThroughTheBox)
{
    Sketch s;
    const Id circle = s.addCircle(s.addPoint({0, 0}), 10.0);
    EXPECT_TRUE(has(inBox(s, same, {9, -1}, {11, 1}, BoxMode::Crossing), circle));
    EXPECT_FALSE(has(inBox(s, same, {-1, -1}, {1, 1}, BoxMode::Crossing), circle));
}

TEST(BoxSelect, ArcSamplesFollowItsSweep)
{
    Sketch s;
    const Id arc = s.addArc(s.addPoint({0, 0}), s.addPoint({10, 0}), s.addPoint({0, 10}));
    const std::vector<Vec2> points = polyline(s, *s.curve(arc));
    ASSERT_GE(points.size(), 9u);
    for (const Vec2 p : points) {
        EXPECT_GE(p.x, -1e-9);
        EXPECT_GE(p.y, -1e-9);
    }
    EXPECT_TRUE(has(inBox(s, same, {-1, -1}, {11, 11}, BoxMode::Window), arc));
}

TEST(BoxSelect, UsesTheProjection)
{
    Sketch s;
    const Id line = s.addLine(s.addPoint({0, 0}), s.addPoint({1, 0}));
    const auto scaled = [](Vec2 p) { return Vec2{p.x * 100.0, -p.y * 100.0}; };
    EXPECT_TRUE(has(inBox(s, scaled, {-5, -5}, {105, 5}, BoxMode::Window), line));
    EXPECT_FALSE(has(inBox(s, scaled, {-5, -5}, {50, 5}, BoxMode::Window), line));
}
