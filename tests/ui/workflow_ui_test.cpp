// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include "app/session/preferences.h"
#include "app/theme/theme.h"
#include "viewer/occt_view_widget.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>

#include <gp_Pnt.hxx>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

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

TEST_F(WorkflowTest, CutFromAFaceSketchGoesIntoTheBody)
{
    block(*ui);
    ui->trigger("Create Sketch");
    ui->click(gp_Pnt(5, 3, kBlockHeight));
    ASSERT_EQ(ui->state()["sketch"], 3) << ui->state().dump(2);
    ui->trigger("Rectangle");
    ui->click(gp_Pnt(-5, -5, kBlockHeight));
    ui->click(gp_Pnt(5, 5, kBlockHeight));
    ui->save("sketch over face");
    ui->trigger("Finish Sketch");

    ui->trigger("Extrude");
    ui->choose("Operation", "Cut");
    ui->save("cut into face");
    ui->fill("Distance", "5");
    const nlohmann::json state = ui->state();
    ASSERT_EQ(state["bodies"].size(), 1U) << state.dump(2);
    EXPECT_NEAR(volume(*ui), kBlockVolume - 10.0 * 10.0 * 5.0, 1e-3) << state.dump(2);
    for (const nlohmann::json& feature : state["features"]) {
        EXPECT_EQ(feature["health"], "ok") << feature.dump();
    }

    ui->doubleClick(ui->timelineChip(4));
    ui->choose("Operation", "Join");
    ui->fill("Distance", "5");
    EXPECT_NEAR(volume(*ui), kBlockVolume + 10.0 * 10.0 * 5.0, 1e-3) << ui->state().dump(2);
}

constexpr int kZoomSteps = 6;
constexpr int kLineSamples = 200;
constexpr int kLineTolerance = 64;

std::vector<QPointF> viewMarks(const UiDriver& ui)
{
    return {ui.at(gp_Pnt(0, 0, 0)), ui.at(gp_Pnt(10, 0, 0)), ui.at(gp_Pnt(0, 10, 0)), ui.at(gp_Pnt(0, 0, 10))};
}

void expectSameView(const std::vector<QPointF>& before, const std::vector<QPointF>& after)
{
    ASSERT_EQ(before.size(), after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_NEAR(before[i].x(), after[i].x(), 1.0) << "mark " << i;
        EXPECT_NEAR(before[i].y(), after[i].y(), 1.0) << "mark " << i;
    }
}

TEST_F(WorkflowTest, FinishingASketchReturnsToThePreviousView)
{
    ui->settle();
    const std::vector<QPointF> before = viewMarks(*ui);

    ui->trigger("Create Sketch");
    ui->click(ui->originPlane(core::OriginPlane::XZ));
    ASSERT_NE(ui->state()["sketch"], 0);
    ui->settle();
    const QPointF up = ui->at(gp_Pnt(0, 0, 10)) - ui->at(gp_Pnt(0, 0, 0));
    const QPointF across = ui->at(gp_Pnt(10, 0, 0)) - ui->at(gp_Pnt(0, 0, 0));
    EXPECT_LT(std::abs(up.x()), 1.0) << "View faces XZ plane";
    EXPECT_LT(std::abs(across.y()), 1.0) << "View faces XZ plane";

    ui->trigger("Finish Sketch");
    ui->settle();
    expectSameView(before, viewMarks(*ui));
}

void zoomOnGridLine(UiDriver& ui)
{
    ui.settle();
    for (int i = 0; i < kZoomSteps; ++i) {
        ui.wheel(ui.at(gp_Pnt(45, 20, 0)), 2);
    }
    ui.settle();
}

void expectSolidAlongGridLine(UiDriver& ui, const QColor& line)
{
    const QImage frame = ui.canvasImage();
    const qreal ratio = ui.canvas()->devicePixelRatioF();
    int sampled = 0;
    int solid = 0;
    for (int i = 0; i <= kLineSamples; ++i) {
        const QPointF at = ui.at(gp_Pnt(30.0 + 30.0 * i / kLineSamples, 20, 0)) * ratio;
        const QPoint pixel = at.toPoint();
        if (!frame.rect().contains(pixel)) {
            continue;
        }
        ++sampled;
        int best = std::numeric_limits<int>::max();
        for (int dy = -1; dy <= 1; ++dy) {
            const QColor c = frame.pixelColor(pixel + QPoint(0, dy));
            best = std::min(best, std::abs(c.red() - line.red()) + std::abs(c.green() - line.green()) +
                                      std::abs(c.blue() - line.blue()));
        }
        solid += best <= kLineTolerance ? 1 : 0;
    }
    ASSERT_GT(sampled, kLineSamples / 2);
    EXPECT_GE(solid, sampled * 9 / 10) << solid << " of " << sampled << " samples show the line";
}

TEST_F(WorkflowTest, FinishedSketchLinesStaySolidOnTheGrid)
{
    sketchRectangle(*ui, core::OriginPlane::XY, gp_Pnt(30, 20, 0), gp_Pnt(60, 40, 0));
    ASSERT_EQ(ui->state()["sketch"], 0);
    zoomOnGridLine(*ui);
    ui->save("zoomed on a grid line");
    expectSolidAlongGridLine(*ui, app::theme::palette().viewport.sketchFree);
}

TEST_F(WorkflowTest, BodyEdgesAndTheirHighlightStaySolidOnTheGrid)
{
    sketchRectangle(*ui, core::OriginPlane::XY, gp_Pnt(30, 20, 0), gp_Pnt(60, 40, 0));
    extrude(*ui, "10");
    zoomOnGridLine(*ui);
    ui->save("body edge on a grid line");
    expectSolidAlongGridLine(*ui, app::theme::palette().viewport.bodyEdge);

    ui->click(gp_Pnt(45, 20, 0));
    ASSERT_TRUE(ui->selectionText().startsWith("1 Edge")) << ui->selectionText().toStdString();
    ui->move(QPointF(20, 20));
    ui->settle();
    ui->save("selected edge on a grid line");
    expectSolidAlongGridLine(*ui, app::theme::palette().chrome.accent);
}

TEST_F(WorkflowTest, WithoutLookAtTheViewStaysPutThroughASketch)
{
    app::Preferences preferences = app::Preferences::load();
    preferences.lookAtSketch = false;
    preferences.save();
    ui = std::make_unique<UiDriver>();
    ui->settle();
    const std::vector<QPointF> before = viewMarks(*ui);

    ui->trigger("Create Sketch");
    ui->click(ui->originPlane(core::OriginPlane::XZ));
    ASSERT_NE(ui->state()["sketch"], 0);
    ui->settle();
    const QPointF up = ui->at(gp_Pnt(0, 0, 10)) - ui->at(gp_Pnt(0, 0, 0));
    EXPECT_GT(std::abs(up.x()) + std::abs(ui->at(gp_Pnt(10, 0, 0)).y() - ui->at(gp_Pnt(0, 0, 0)).y()), 1.0)
        << "View keeps its slant instead of facing plane";

    ui->trigger("Finish Sketch");
    ui->settle();
    expectSameView(before, viewMarks(*ui));
}

} // namespace
} // namespace kalve137::uitest
