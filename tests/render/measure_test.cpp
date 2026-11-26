// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/measure.h"
#include "render/style.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using kalve137::render::DatumLine;
using kalve137::render::kDashPattern;
using kalve137::render::kMeasureLineWidth;
using kalve137::render::MeasureItems;
using kalve137::render::MeasureLine;
using kalve137::render::placeMeasureLines;
using kalve137::render::vertexColor;

TEST(MeasureLines, PlaceTheSpanDashedAroundTheSceneOrigin)
{
    const MeasureItems items{
        .line = MeasureLine{.from = {110.0, 20.0, 5.0}, .to = {100.0, 20.0, 35.0}, .color = {1.0F, 0.7F, 0.0F, 1.0F}},
        .markers = {}};

    const std::vector<DatumLine> lines = placeMeasureLines(items, {100.0, 20.0, 5.0});

    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0].start, (kalve137::render::Position{10.0F, 0.0F, 0.0F}));
    EXPECT_EQ(lines[0].end, (kalve137::render::Position{0.0F, 0.0F, 30.0F}));
    EXPECT_EQ(lines[0].color, vertexColor(items.line->color));
    EXPECT_FLOAT_EQ(lines[0].width, static_cast<float>(kMeasureLineWidth));
    EXPECT_FLOAT_EQ(lines[0].pattern, static_cast<float>(kDashPattern));
}

TEST(MeasureLines, PlaceNothingWithoutASpan)
{
    EXPECT_TRUE(placeMeasureLines({}, {}).empty());
}

} // namespace
