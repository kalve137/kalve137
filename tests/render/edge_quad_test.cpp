// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/edge_quad.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <optional>

namespace kalve137::render {
namespace {

constexpr double kWidth = 400.0;
constexpr double kHeight = 200.0;
constexpr double kPixel = 1e-9;

std::array<double, 2> pixelOf(const Clip& clip)
{
    return {(clip[0] / clip[3] + 1.0) * kWidth * 0.5, (clip[1] / clip[3] + 1.0) * kHeight * 0.5};
}

std::array<std::array<double, 2>, kEdgeCorners> cornersOf(const Clip& start, const Clip& end, double widthPixels)
{
    std::array<std::array<double, 2>, kEdgeCorners> corners{};
    for (int corner = 0; corner < kEdgeCorners; ++corner) {
        const std::optional<Clip> clip = expandEdge(start, end, corner, widthPixels, kWidth, kHeight);
        EXPECT_TRUE(clip);
        corners.at(corner) = clip ? pixelOf(*clip) : std::array<double, 2>{};
    }
    return corners;
}

TEST(EdgeQuad, ACoversTheLineAtItsWidthInPixelsAndHalfAWidthPastEachEnd)
{
    const Clip start{-0.5, 0.0, 0.25, 1.0};
    const Clip end{0.5, 0.0, 0.75, 1.0};
    const auto corners = cornersOf(start, end, 3.0);
    EXPECT_NEAR(corners[0][0], 98.5, kPixel);
    EXPECT_NEAR(corners[0][1], 98.5, kPixel);
    EXPECT_NEAR(corners[1][1], 101.5, kPixel);
    EXPECT_NEAR(corners[3][0], 301.5, kPixel);
    EXPECT_NEAR(corners[3][1], 101.5, kPixel);
    EXPECT_EQ(expandEdge(start, end, 2, 3.0, kWidth, kHeight)->at(2), end[2]) << "edges keep their true depth";
}

TEST(EdgeQuad, TheWidthStaysInPixelsOnAStretchedViewportAndInPerspective)
{
    const Clip start{-0.5, -0.5, 0.5, 2.0};
    const Clip end{1.0, 1.5, 0.5, 4.0};
    const auto corners = cornersOf(start, end, 2.0);
    const double across = std::hypot(corners[1][0] - corners[0][0], corners[1][1] - corners[0][1]);
    EXPECT_NEAR(across, 2.0, 1e-6);
    const double dx = corners[2][0] - corners[0][0];
    const double dy = corners[2][1] - corners[0][1];
    const double sideX = corners[1][0] - corners[0][0];
    const double sideY = corners[1][1] - corners[0][1];
    EXPECT_NEAR((dx * sideX) + (dy * sideY), 0.0, 1e-6) << "sides stand square to the line";
}

TEST(EdgeQuad, AnEdgeBehindTheEyeIsDroppedAndOneCrossingItIsCut)
{
    EXPECT_FALSE(expandEdge({0.0, 0.0, 0.0, -1.0}, {1.0, 0.0, 0.0, -2.0}, 0, 1.0, kWidth, kHeight));
    const std::optional<Clip> cut = expandEdge({0.0, 0.0, 0.0, 1.0}, {0.0, 1.0, 0.0, -1.0}, 2, 1.0, kWidth, kHeight);
    ASSERT_TRUE(cut);
    EXPECT_GT(cut->at(3), 0.0);
    EXPECT_TRUE(std::isfinite(pixelOf(*cut)[1]));
}

TEST(EdgeQuad, ALineSeenEndOnStillCoversItsWidth)
{
    const Clip point{0.0, 0.0, 0.5, 1.0};
    const auto corners = cornersOf(point, point, 2.0);
    EXPECT_NEAR(corners[3][0] - corners[0][0], 2.0, kPixel);
    EXPECT_NEAR(corners[3][1] - corners[0][1], 2.0, kPixel);
}

} // namespace
} // namespace kalve137::render
