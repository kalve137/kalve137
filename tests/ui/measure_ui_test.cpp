// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pair_shots.h"
#include "scenario.h"
#include "ui_test.h"

#include "app/feature/measure_panel.h"
#include "app/shell/main_window.h"
#include "app/view/pick.h"
#include "app/view/viewport_picker.h"
#include "render/draw_list.h"
#include "render/rhi/viewport.h"
#include "snapshot/body.h"

#include <QImage>
#include <QLabel>
#include <QPointF>
#include <QString>

#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace kalve137::uitest {
namespace {

constexpr int kChangedPixels = 10;
constexpr double kOnPoint = 1.0;
constexpr QPointF kAwayFromBlock{6.0, 6.0};
const gp_Pnt kTopFace(5, 3, kBlockHeight);
const gp_Pnt kBottomCorner(-kBlockWidth / 2, -kBlockDepth / 2, 0);
const gp_Pnt kTopCorner(-kBlockWidth / 2, -kBlockDepth / 2, kBlockHeight);

QString labelText(const app::MeasurePanel& panel, bool table)
{
    for (const QLabel* label : panel.findChildren<QLabel*>()) {
        const bool isTable = label->text().startsWith(QStringLiteral("<table>"));
        if (label->objectName() == QStringLiteral("panelHint") && !table) {
            return label->text();
        }
        if (isTable && table) {
            return label->text();
        }
    }
    return {};
}

double pixelsBetween(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

std::ptrdiff_t markersAt(const UiDriver& ui, const render::Viewport& canvas, const gp_Pnt& point)
{
    const snapshot::Point& origin = canvas.scene().origin;
    const QPointF target = ui.at(point);
    return std::ranges::count_if(canvas.markerDraw().markers, [&](const render::Marker& marker) {
        const gp_Pnt at(origin[0] + marker.position[0], origin[1] + marker.position[1], origin[2] + marker.position[2]);
        return pixelsBetween(ui.at(at), target) <= kOnPoint;
    });
}

using MeasureTest = UiTest;

TEST_F(MeasureTest, ShowsDistanceBetweenTwoPicks)
{
    block(*ui);
    homeView(*ui);
    auto* panel = ui->window().findChild<app::MeasurePanel*>();
    ASSERT_NE(panel, nullptr);
    ui->trigger("Measure");
    ASSERT_TRUE(panel->isActive());

    ui->move(kAwayFromBlock);
    const QImage away = ui->canvasImage();
    ui->move(ui->at(kBottomCorner));
    const QImage hovered = ui->canvasImage();
    ui->save("measure hovers a vertex");
    if (ui->viewport() != nullptr) {
        auto* picker = ui->window().findChild<app::ViewportPicker*>();
        ASSERT_NE(picker, nullptr);
        ASSERT_TRUE(picker->hovered());
        EXPECT_EQ(picker->hovered()->kind, app::Pick::Kind::Vertex);
        EXPECT_GE(markerChangeAround(away, hovered, ui->at(kBottomCorner)), kChangedPixels) << "hovered vertex shows";
    }

    ui->click(kTopFace);
    ui->click(kBottomCorner);
    ui->move(kAwayFromBlock);
    ui->save("measure");
    EXPECT_EQ(labelText(*panel, false), QStringLiteral("Face + Vertex"));
    const QString table = labelText(*panel, true);
    EXPECT_TRUE(table.contains(QStringLiteral("Distance"))) << table.toStdString();
    EXPECT_TRUE(table.contains(QStringLiteral("15.000 mm"))) << table.toStdString();

    if (render::Viewport* canvas = ui->viewport()) {
        const QPointF vertex = ui->at(kBottomCorner) * ui->canvasImage().devicePixelRatio();
        const std::ptrdiff_t atVertex = markersAt(*ui, *canvas, kBottomCorner);
        const std::ptrdiff_t atFace = markersAt(*ui, *canvas, kTopCorner);
        std::printf("picked vertex at pixel %.0f %.0f: %td markers there, %td at the face end\n", vertex.x(),
                    vertex.y(), atVertex, atFace);
        EXPECT_EQ(atVertex, 1) << "picker's ball alone marks the picked vertex";
        EXPECT_EQ(atFace, 1) << "measure marks the face end";
        ASSERT_EQ(canvas->vertexMarks().size(), 1U);
        EXPECT_EQ(canvas->vertexMarks().front().shape, render::MarkerShape::Ball);

        const render::MeasureItems& measure = canvas->measure();
        ASSERT_TRUE(measure.line);
        ASSERT_EQ(measure.markers.size(), 1U);
        const render::MarkerItem& marker = measure.markers.front();
        EXPECT_EQ(marker.shape, render::MarkerShape::Ball);
        EXPECT_EQ(marker.scale, render::MarkerScale::Small);
        EXPECT_EQ(marker.flags, render::kMarkerTopmost);
        const QPointF drawn = ui->at(gp_Pnt(marker.anchor.x, marker.anchor.y, marker.anchor.z));
        EXPECT_LE(pixelsBetween(drawn, ui->at(kTopCorner)), kOnPoint);
        auto* picker = ui->window().findChild<app::ViewportPicker*>();
        ASSERT_NE(picker, nullptr);
        EXPECT_EQ(picker->panelItems().selected.size(), 2U) << "face and the vertex show picked";
    }

    ui->pressButton("Close");
    EXPECT_FALSE(panel->isActive());
    if (render::Viewport* canvas = ui->viewport()) {
        EXPECT_EQ(canvas->measure(), render::MeasureItems{});
        EXPECT_TRUE(ui->window().findChild<app::ViewportPicker*>()->panelItems().selected.empty());
    }
}

} // namespace
} // namespace kalve137::uitest
