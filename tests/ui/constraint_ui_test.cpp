// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include "core/feature.h"
#include "sketch/sketch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <variant>
#include <vector>

namespace kalve137::uitest {
namespace {

using ConstraintTest = UiTest;

constexpr double kTolerance = 1e-6;

TEST_F(ConstraintTest, ConcentricJoinsTwoCircleCenters)
{
    ui->trigger("Create Sketch");
    ui->click(ui->originPlane(core::OriginPlane::XY));
    ASSERT_NE(ui->state()["sketch"], 0) << ui->state().dump(2);
    ui->trigger("Circle");
    ui->click(gp_Pnt(-20, 0, 0));
    ui->click(gp_Pnt(-10, 0, 0));
    ui->click(gp_Pnt(25, 5, 0));
    ui->click(gp_Pnt(31, 5, 0));

    ui->trigger("Concentric");
    ui->click(gp_Pnt(-10, 0, 0));
    ui->click(gp_Pnt(31, 5, 0));
    ui->save("concentric");
    ui->trigger("Finish Sketch");

    ASSERT_FALSE(ui->document().features().empty());
    const auto* feature = std::get_if<core::SketchFeature>(&ui->document().features().back().data);
    ASSERT_NE(feature, nullptr);
    const sketch::Sketch& s = feature->sketch;
    std::vector<sketch::Id> centers;
    for (const auto& [id, curve] : s.curves()) {
        ASSERT_EQ(curve.kind, sketch::CurveKind::Circle);
        centers.push_back(curve.points.front());
    }
    ASSERT_EQ(centers.size(), 2U);
    int joins = 0;
    for (const auto& [id, constraint] : s.constraints()) {
        if (constraint.kind == sketch::ConstraintKind::Coincident) {
            EXPECT_EQ(constraint.refs.size(), 2U);
            EXPECT_TRUE(std::ranges::is_permutation(constraint.refs, centers));
            ++joins;
        }
    }
    EXPECT_EQ(joins, 1);
    const sketch::Vec2 a = s.point(centers[0])->pos;
    const sketch::Vec2 b = s.point(centers[1])->pos;
    EXPECT_NEAR(a.x, b.x, kTolerance);
    EXPECT_NEAR(a.y, b.y, kTolerance);
}

} // namespace
} // namespace kalve137::uitest
