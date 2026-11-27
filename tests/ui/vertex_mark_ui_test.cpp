// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pair_shots.h"
#include "scenario.h"
#include "ui_test.h"

#include "app/shell/main_window.h"
#include "app/view/panel_picks.h"
#include "app/view/pick.h"
#include "app/view/viewport_picker.h"
#include "render/draw_list.h"
#include "render/rhi/viewport.h"
#include "snapshot/body.h"

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRgb>

#include <TopoDS.hxx>
#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr int kChangedPixels = 10;
constexpr int kChange = 10;
constexpr QPointF kAwayFromBlock{6.0, 6.0};
const gp_Pnt kBottomCorner(-kBlockWidth / 2, -kBlockDepth / 2, 0);
const gp_Pnt kTopFace(5, 3, kBlockHeight);
constexpr snapshot::Color kWhite{1.0F, 1.0F, 1.0F, 1.0F};

using VertexMarkTest = UiTest;

TEST_F(VertexMarkTest, AHoveredVertexShowsARingAndAPickedOneABall)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    auto* picker = ui->window().findChild<app::ViewportPicker*>();
    ASSERT_NE(picker, nullptr);
    block(*ui);
    homeView(*ui);
    ui->trigger("Measure");

    ui->move(kAwayFromBlock);
    const QImage away = ui->canvasImage();
    EXPECT_TRUE(canvas->vertexMarks().empty());
    ui->move(ui->at(kBottomCorner));
    const QImage hovered = ui->canvasImage();
    ui->save("vertex hovered");
    const std::optional<app::Pick> pick = picker->hovered();
    ASSERT_TRUE(pick);
    ASSERT_EQ(pick->kind, app::Pick::Kind::Vertex);
    ASSERT_EQ(canvas->vertexMarks().size(), 1U);
    EXPECT_EQ(canvas->vertexMarks().front().shape, render::MarkerShape::Ring);
    const int ring = markerChangeAround(away, hovered, ui->at(kBottomCorner));
    std::printf("hovered vertex: %d pixels changed by %d or more\n", ring, kChange);
    EXPECT_GE(ring, kChangedPixels) << "hovered vertex shows";

    ui->pressButton("Close");
    ui->move(kAwayFromBlock);
    EXPECT_TRUE(canvas->vertexMarks().empty()) << "ring goes with the hover";
    picker->setPanelPicks({.vertices = {TopoDS::Vertex(pick->shape)}});
    ui->frame();
    const QImage picked = ui->canvasImage();
    ui->save("vertex picked");
    ASSERT_EQ(canvas->vertexMarks().size(), 1U);
    EXPECT_EQ(canvas->vertexMarks().front().shape, render::MarkerShape::Ball);
    const int ball = markerChangeAround(away, picked, ui->at(kBottomCorner));
    std::printf("picked vertex: %d pixels changed by %d or more\n", ball, kChange);
    EXPECT_GE(ball, kChangedPixels) << "picked vertex shows";

    picker->setPanelPicks({});
    EXPECT_TRUE(canvas->vertexMarks().empty());
}

TEST_F(VertexMarkTest, AHoveredWhiteBodyChangesByTenOrMorePerChannel)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    block(*ui);
    homeView(*ui);
    snapshot::Scene scene = canvas->scene();
    ASSERT_EQ(scene.bodies.size(), 1U);
    snapshot::Body white = *scene.bodies.front();
    white.appearance.color = kWhite;
    scene.bodies.front() = std::make_shared<const snapshot::Body>(std::move(white));
    canvas->setScene(std::move(scene));

    ui->move(kAwayFromBlock);
    const QImage plain = ui->canvasImage();
    ui->move(ui->at(kTopFace));
    const QImage hovered = ui->canvasImage();
    ui->save("white body hovered");
    const double ratio = plain.devicePixelRatio();
    const QPoint at((ui->at(kTopFace) * ratio).toPoint());
    const QRgb before = plain.pixel(at);
    const QRgb after = hovered.pixel(at);
    const int red = std::abs(qRed(before) - qRed(after));
    const int green = std::abs(qGreen(before) - qGreen(after));
    const int blue = std::abs(qBlue(before) - qBlue(after));
    std::printf("white body: %s plain, %s hovered, change %d %d %d\n", hex(before).c_str(), hex(after).c_str(), red,
                green, blue);
    EXPECT_GE(std::min({red, green, blue}), kChange) << "every channel moves on hover";
}

} // namespace
} // namespace kalve137::uitest
