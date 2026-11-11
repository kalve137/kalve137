// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include <gtest/gtest.h>

#include <QElapsedTimer>

#include <algorithm>
#include <cstdio>
#include <numbers>

namespace kalve137::uitest {
namespace {

constexpr double kTolerance = 1e-3;
const gp_Pnt kTopFrontEdge(0, -kBlockDepth / 2, kBlockHeight);
const gp_Pnt kTopFace(5, 3, kBlockHeight);

class FeatureTest : public UiTest {
protected:
    void SetUp() override
    {
        UiTest::SetUp();
        block(*ui);
        ASSERT_NEAR(volume(*ui), kBlockVolume, kTolerance) << ui->state().dump(2);
    }

    void expectHealthy()
    {
        const nlohmann::json state = ui->state();
        for (const nlohmann::json& feature : state["features"]) {
            EXPECT_EQ(feature["health"], "ok") << feature.dump();
        }
    }
};

TEST_F(FeatureTest, FilletsAnEdge)
{
    ui->trigger("Fillet");
    ui->click(kTopFrontEdge);
    ui->save("fillet picked");
    ui->fill("Radius", "2");
    ui->save("fillet");
    const double removed = (4.0 - std::numbers::pi) * kBlockWidth;
    EXPECT_NEAR(volume(*ui), kBlockVolume - removed, kTolerance) << ui->state().dump(2);
    EXPECT_EQ(faces(*ui), 7);
    expectHealthy();
}

TEST_F(FeatureTest, FilletsTwoSetsWithTheirOwnRadius)
{
    ui->trigger("Fillet");
    ui->click(kTopFrontEdge);
    ui->fill("Radius", "2", false);
    ui->pressButton("Add set");
    ASSERT_NE(ui->field("Set"), nullptr) << "the set chooser shows once there are two sets";
    ui->click(gp_Pnt(0, kBlockDepth / 2, kBlockHeight));
    ui->save("second set");
    ui->fill("Radius", "3");
    ui->save("fillet sets");
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    const double corner = 1.0 - std::numbers::pi / 4.0;
    EXPECT_NEAR(volume(*ui), kBlockVolume - corner * kBlockWidth * (4.0 + 9.0), kTolerance) << ui->state().dump(2);
    EXPECT_EQ(faces(*ui), 8);
    expectHealthy();
}

TEST_F(FeatureTest, CutsThroughAll)
{
    sketchRectangle(*ui, core::OriginPlane::XY, gp_Pnt(-5, -5, 0), gp_Pnt(5, 5, 0));
    ui->trigger("Extrude");
    ui->choose("End", "All");
    ui->choose("Operation", "Cut");
    ui->save("cut through all");
    ui->pressButton("OK");
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    EXPECT_NEAR(volume(*ui), kBlockVolume - 100.0 * kBlockHeight, kTolerance) << ui->state().dump(2);
    expectHealthy();
}

TEST_F(FeatureTest, ExtrudesUpToAFaceWithATaper)
{
    sketchRectangle(*ui, core::OriginPlane::XY, gp_Pnt(24, -5, 0), gp_Pnt(34, 5, 0));
    ui->trigger("Extrude");
    ui->choose("End", "To object");
    ui->click(kTopFace);
    ui->save("up to the top face");
    ui->fill("Taper", "5");
    ui->save("tapered");
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    const double top = 10.0 - 2.0 * kBlockHeight * std::tan(5.0 * std::numbers::pi / 180.0);
    EXPECT_NEAR(volume(*ui, 1), kBlockHeight / 3.0 * (100.0 + top * top + 10.0 * top), kTolerance)
        << ui->state().dump(2);
    expectHealthy();
}

TEST_F(FeatureTest, ChamfersAnEdge)
{
    ui->trigger("Chamfer");
    ui->click(kTopFrontEdge);
    ui->fill("Distance", "2");
    ui->save("chamfer");
    EXPECT_NEAR(volume(*ui), kBlockVolume - 2.0 * kBlockWidth, kTolerance) << ui->state().dump(2);
    EXPECT_EQ(faces(*ui), 7);
    expectHealthy();
}

TEST_F(FeatureTest, ShellsOpenAtTheTop)
{
    ui->trigger("Shell");
    ui->click(kTopFace);
    ui->save("shell picked");
    ui->fill("Thickness", "2");
    ui->save("shell");
    const double inside = (kBlockWidth - 4) * (kBlockDepth - 4) * (kBlockHeight - 2);
    EXPECT_NEAR(volume(*ui), kBlockVolume - inside, kTolerance) << ui->state().dump(2);
    expectHealthy();
}

TEST_F(FeatureTest, PressPullsTheTopFace)
{
    ui->trigger("Press Pull");
    ui->click(kTopFace);
    ui->save("press pull picked");
    ui->fill("Distance", "5");
    ui->save("press pull");
    EXPECT_NEAR(volume(*ui), kBlockWidth * kBlockDepth * (kBlockHeight + 5), kTolerance) << ui->state().dump(2);
    expectHealthy();
}

TEST_F(FeatureTest, DrillsAHoleInTheTopFace)
{
    ui->trigger("Hole");
    ui->click(gp_Pnt(0, 0, kBlockHeight));
    ui->save("hole picked");
    ui->fill("Diameter", "6", false);
    ui->fill("Depth", "8");
    ui->save("hole");
    EXPECT_LT(volume(*ui), kBlockVolume - std::numbers::pi * 9.0 * 8.0 + kTolerance) << ui->state().dump(2);
    EXPECT_GT(faces(*ui), 6);
    expectHealthy();
}

TEST_F(FeatureTest, UndoesToEmptyAndRedoesToTheEnd)
{
    ui->trigger("Fillet");
    ui->click(kTopFrontEdge);
    ui->fill("Radius", "2");
    const double filleted = volume(*ui);
    const std::size_t features = ui->document().features().size();
    ASSERT_NE(ui->action("Undo"), nullptr);
    ASSERT_NE(ui->action("Redo"), nullptr);
    while (ui->action("Undo")->isEnabled()) {
        ui->trigger("Undo");
    }
    EXPECT_TRUE(ui->document().features().empty()) << ui->state().dump(2);
    EXPECT_TRUE(ui->state()["bodies"].empty());
    while (ui->action("Redo")->isEnabled()) {
        ui->trigger("Redo");
    }
    EXPECT_EQ(ui->document().features().size(), features);
    EXPECT_NEAR(volume(*ui), filleted, kTolerance);
    expectHealthy();
}

TEST_F(FeatureTest, EscapeCancelsAFeatureWithoutChanges)
{
    ui->trigger("Fillet");
    ui->click(kTopFrontEdge);
    ASSERT_TRUE(ui->state()["featurePanel"]);
    ui->key(Qt::Key_Escape);
    EXPECT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    EXPECT_NEAR(volume(*ui), kBlockVolume, kTolerance);
    EXPECT_EQ(ui->document().features().size(), 2U);
}

TEST_F(FeatureTest, ClosesWithEveryCommandOpen)
{
    for (const char* command :
         {"Extrude", "Revolve", "Fillet", "Chamfer", "Shell", "Press Pull", "Hole", "Move", "Appearance", "Pattern",
          "Offset Plane", "Plane at Angle", "Midplane", "Axis", "Point", "Measure", "Create Sketch"}) {
        SCOPED_TRACE(command);
        ui->trigger(command);
        ui->click(kTopFace);
        ui.reset();
        ui = std::make_unique<UiDriver>();
        block(*ui);
    }
}

TEST_F(FeatureTest, IdleHoverAndSelectStayFast)
{
    constexpr int kRounds = 10;
    constexpr qint64 kBudgetMs = 150;
    const QPointF face = ui->at(kTopFace);
    const QPointF empty(20, 20);
    qint64 worstHover = 0;
    qint64 worstSelect = 0;
    for (int i = 0; i < kRounds; ++i) {
        ui->move(empty);
        QElapsedTimer timer;
        timer.start();
        ui->move(face);
        worstHover = std::max(worstHover, timer.elapsed());
        timer.restart();
        ui->click(face);
        worstSelect = std::max(worstSelect, timer.elapsed());
        ASSERT_FALSE(ui->selectionText().isEmpty());
        ui->key(Qt::Key_Escape);
    }
    RecordProperty("worst_hover_ms", static_cast<int>(worstHover));
    RecordProperty("worst_select_ms", static_cast<int>(worstSelect));
    std::printf("worst hover %lld ms, worst select %lld ms\n", worstHover, worstSelect);
#ifdef NDEBUG
    EXPECT_LT(worstHover, kBudgetMs);
    EXPECT_LT(worstSelect, kBudgetMs);
#endif
}

TEST_F(FeatureTest, IdleClickSelectsAFaceAndEscapeClears)
{
    ui->click(kTopFace);
    ui->save("face selected");
    EXPECT_FALSE(ui->selectionText().isEmpty()) << ui->state().dump(2);
    ui->key(Qt::Key_Escape);
    EXPECT_TRUE(ui->selectionText().isEmpty()) << ui->selectionText().toStdString();
}

} // namespace
} // namespace kalve137::uitest
