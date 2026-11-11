// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include <gtest/gtest.h>

namespace kalve137::uitest {
namespace {

using WorkflowTest = UiTest;

TEST_F(WorkflowTest, SketchesARectangleAndExtrudesIt)
{
    ui->trigger("Create Sketch");
    ui->save("picking plane");
    ui->click(ui->originPlane(core::OriginPlane::XY));
    ASSERT_NE(ui->state()["sketch"], 0) << ui->state().dump(2);
    ui->save("sketching");

    ui->trigger("Rectangle");
    ui->click(gp_Pnt(-20, -10, 0));
    ui->move(ui->at(gp_Pnt(0, 0, 0)));
    ui->save("rectangle preview");
    ui->click(gp_Pnt(20, 10, 0));
    ui->save("rectangle");
    ui->trigger("Finish Sketch");
    ASSERT_EQ(ui->state()["sketch"], 0) << ui->state().dump(2);

    ui->trigger("Extrude");
    ASSERT_TRUE(ui->state()["featurePanel"]) << ui->state().dump(2);
    ui->save("extrude");
    ui->fill("Distance", "15");
    EXPECT_FALSE(ui->state()["featurePanel"]) << "Enter in the distance field accepts";
    const nlohmann::json state = ui->state();
    ASSERT_EQ(state["bodies"].size(), 1U) << state.dump(2);
    EXPECT_NEAR(state["bodies"][0]["volume"].get<double>(), 40.0 * 20.0 * 15.0, 1e-6) << state.dump(2);
    EXPECT_EQ(state["bodies"][0]["faces"], 6);
    ui->save("body");
}

TEST_F(WorkflowTest, DimensionsTheSketchAndEditsItFromTheTimeline)
{
    sketchRectangle(*ui, core::OriginPlane::XY, gp_Pnt(-20, -10, 0), gp_Pnt(20, 10, 0));
    ui->doubleClick(ui->timelineChip(1));
    ASSERT_EQ(ui->state()["sketch"], 1) << ui->state().dump(2);

    ui->trigger("Dimension");
    ui->click(gp_Pnt(0, -10, 0));
    ui->click(gp_Pnt(5, -12, 0));
    ui->save("dimension editor");
    ui->enterValue("50");
    ui->save("dimensioned");
    ui->trigger("Finish Sketch");
    extrude(*ui, "10");
    ASSERT_EQ(ui->state()["bodies"].size(), 1U) << ui->state().dump(2);
    EXPECT_NEAR(ui->state()["bodies"][0]["volume"].get<double>(), 50.0 * 20.0 * 10.0, 1e-6);

    ui->doubleClick(ui->timelineChip(1));
    ASSERT_EQ(ui->state()["sketch"], 1) << ui->state().dump(2);
    ui->trigger("Select");
    ui->save("editing sketch");
    const nlohmann::json label = ui->state()["dimensions"][0]["label"];
    ASSERT_TRUE(label.is_array()) << ui->state().dump(2);
    ui->doubleClick(ui->at(gp_Pnt(label[0].get<double>(), label[1].get<double>(), label[2].get<double>())));
    ui->save("dimension edit");
    ui->enterValue("60");
    ui->trigger("Finish Sketch");
    const nlohmann::json state = ui->state();
    ASSERT_EQ(state["bodies"].size(), 1U) << state.dump(2);
    EXPECT_NEAR(state["bodies"][0]["volume"].get<double>(), 60.0 * 20.0 * 10.0, 1e-6) << state.dump(2);
}

TEST_F(WorkflowTest, SketchOnAFaceFollowsTheFace)
{
    block(*ui);
    ui->trigger("Create Sketch");
    ui->click(gp_Pnt(5, 3, kBlockHeight));
    ASSERT_EQ(ui->state()["sketch"], 3) << ui->state().dump(2);
    ui->trigger("Rectangle");
    ui->click(gp_Pnt(-5, -5, kBlockHeight));
    ui->click(gp_Pnt(5, 5, kBlockHeight));
    ui->save("sketch on face");
    ui->trigger("Finish Sketch");
    extrude(*ui, "5");
    const auto total = [&] {
        const nlohmann::json state = ui->state();
        double sum = 0.0;
        for (const nlohmann::json& body : state["bodies"]) {
            sum += body["volume"].get<double>();
        }
        return sum;
    };
    ASSERT_NEAR(total(), kBlockVolume + 10.0 * 10.0 * 5.0, 1e-3) << ui->state().dump(2);

    ui->doubleClick(ui->timelineChip(2));
    ASSERT_TRUE(ui->state()["featurePanel"]) << ui->state().dump(2);
    ui->fill("Distance", "25");
    ui->save("base grown");
    const nlohmann::json state = ui->state();
    EXPECT_NEAR(total(), kBlockWidth * kBlockDepth * 25.0 + 10.0 * 10.0 * 5.0, 1e-3) << state.dump(2);
    for (const nlohmann::json& feature : state["features"]) {
        EXPECT_EQ(feature["health"], "ok") << feature.dump();
    }
}

} // namespace
} // namespace kalve137::uitest
