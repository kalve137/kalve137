// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include <gtest/gtest.h>

#include <variant>

namespace kalve137::uitest {
namespace {

constexpr double kTolerance = 1e-3;
constexpr core::FeatureId kExtrude = 2;
const gp_Pnt kTopFace(5, 3, kBlockHeight);

class MenuTest : public UiTest {
protected:
    void SetUp() override
    {
        UiTest::SetUp();
        block(*ui);
        ASSERT_NEAR(volume(*ui), kBlockVolume, kTolerance) << ui->state().dump(2);
    }

    double totalVolume() const
    {
        double total = 0.0;
        const nlohmann::json state = ui->state();
        for (const nlohmann::json& body : state["bodies"]) {
            total += body["volume"].get<double>();
        }
        return total;
    }
};

TEST_F(MenuTest, TimelineSuppressesAndUnsuppresses)
{
    ui->contextMenu(ui->timelineChip(kExtrude), "Suppress");
    nlohmann::json state = ui->state();
    EXPECT_TRUE(state["features"][1]["suppressed"]) << state.dump(2);
    EXPECT_TRUE(state["bodies"].empty()) << state.dump(2);
    ui->save("suppressed");
    ui->contextMenu(ui->timelineChip(kExtrude), "Unsuppress");
    EXPECT_NEAR(volume(*ui), kBlockVolume, kTolerance) << ui->state().dump(2);
}

TEST_F(MenuTest, TimelineRenames)
{
    answerText("Base");
    ui->contextMenu(ui->timelineChip(kExtrude), "Rename...");
    EXPECT_EQ(ui->state()["features"][1]["name"], "Base") << ui->state().dump(2);
    ui->trigger("Undo");
    EXPECT_EQ(ui->state()["features"][1]["name"], "Extrude1");
}

TEST_F(MenuTest, TimelineRollsBackAndToTheEnd)
{
    ui->contextMenu(ui->timelineChip(kExtrude), "Roll Back Before This");
    EXPECT_TRUE(ui->state()["bodies"].empty()) << ui->state().dump(2);
    EXPECT_EQ(ui->state()["features"][1]["health"], "rolled back");
    ui->save("rolled back");
    ui->contextMenu(ui->timelineChip(kExtrude), "Roll To End");
    EXPECT_NEAR(volume(*ui), kBlockVolume, kTolerance) << ui->state().dump(2);
}

TEST_F(MenuTest, TimelineDeletesAndUndoBringsItBack)
{
    ui->contextMenu(ui->timelineChip(kExtrude), "Delete");
    EXPECT_EQ(ui->state()["features"].size(), 1U) << ui->state().dump(2);
    EXPECT_TRUE(ui->state()["bodies"].empty());
    ui->trigger("Undo");
    EXPECT_NEAR(volume(*ui), kBlockVolume, kTolerance) << ui->state().dump(2);
}

TEST_F(MenuTest, TimelineEditsTheExtrude)
{
    ui->contextMenu(ui->timelineChip(kExtrude), "Edit");
    ASSERT_TRUE(ui->state()["featurePanel"]) << ui->state().dump(2);
    ui->fill("Distance", "20");
    EXPECT_NEAR(volume(*ui), kBlockWidth * kBlockDepth * 20.0, kTolerance) << ui->state().dump(2);
}

TEST_F(MenuTest, BrowserHidesAndShowsABody)
{
    ui->contextMenu(ui->browserItem("Body1"), "Hide");
    EXPECT_FALSE(ui->state()["bodies"][0]["visible"]) << ui->state().dump(2);
    ui->save("hidden");
    ui->contextMenu(ui->browserItem("Body1"), "Show");
    EXPECT_TRUE(ui->state()["bodies"][0]["visible"]) << ui->state().dump(2);
}

TEST_F(MenuTest, BrowserEditsTheSketch)
{
    ui->contextMenu(ui->browserItem("Sketch1"), "Edit Sketch");
    EXPECT_EQ(ui->state()["sketch"], 1) << ui->state().dump(2);
    ui->trigger("Finish Sketch");
    EXPECT_EQ(ui->state()["sketch"], 0);
    EXPECT_NEAR(volume(*ui), kBlockVolume, kTolerance) << ui->state().dump(2);
}

TEST_F(MenuTest, BrowserOpensAppearance)
{
    ui->contextMenu(ui->browserItem("Body1"), "Appearance...");
    EXPECT_TRUE(ui->state()["featurePanel"]) << ui->state().dump(2);
    ui->save("appearance");
    ui->key(Qt::Key_Escape);
    EXPECT_FALSE(ui->state()["featurePanel"]);
    EXPECT_EQ(ui->state()["features"].size(), 2U);
}

TEST_F(MenuTest, MovesTheBlockUp)
{
    ui->trigger("Move");
    ui->click(kTopFace);
    ui->save("move picked");
    ui->fill("Z distance", "5");
    ASSERT_EQ(ui->document().features().size(), 3U) << ui->state().dump(2);
    const auto* move = std::get_if<core::MoveFeature>(&ui->document().features().back().data);
    ASSERT_NE(move, nullptr);
    EXPECT_NEAR(move->translation.Z(), 5.0, kTolerance);
    EXPECT_EQ(move->bodies.size(), 1U);
    EXPECT_NEAR(volume(*ui), kBlockVolume, kTolerance);
    ui->save("moved");
}

TEST_F(MenuTest, PatternsTheBlockAlongX)
{
    ui->trigger("Pattern");
    ui->click(kTopFace);
    ui->save("pattern picked");
    ui->fill("Spacing", "50");
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    EXPECT_NEAR(totalVolume(), 3.0 * kBlockVolume, kTolerance) << ui->state().dump(2);
    ui->save("patterned");
}

} // namespace
} // namespace kalve137::uitest
