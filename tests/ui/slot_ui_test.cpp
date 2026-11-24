// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include "sketch/sketch.h"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace kalve137::uitest {
namespace {

using SlotTest = UiTest;

constexpr double kTolerance = 1e-6;
constexpr int kPreviewPixels = 40;
constexpr double kCursorClearPixels = 24.0;

std::vector<const sketch::Curve*> arcsOf(const sketch::Sketch& s)
{
    std::vector<const sketch::Curve*> arcs;
    for (const auto& [id, curve] : s.curves()) {
        if (curve.kind == sketch::CurveKind::Arc) {
            arcs.push_back(&curve);
        }
    }
    std::ranges::sort(arcs, [&](const sketch::Curve* a, const sketch::Curve* b) {
        return s.point(a->points[0])->pos.x < s.point(b->points[0])->pos.x;
    });
    return arcs;
}

void expectSlot(const sketch::Sketch& s, sketch::Vec2 start, sketch::Vec2 end, double width)
{
    const std::vector<const sketch::Curve*> arcs = arcsOf(s);
    ASSERT_EQ(arcs.size(), 2U);
    int sides = 0;
    for (const auto& [id, curve] : s.curves()) {
        if (curve.kind == sketch::CurveKind::Line && !curve.construction) {
            ++sides;
        }
    }
    EXPECT_EQ(sides, 2);
    const sketch::Vec2 centers[2] = {start, end};
    for (int i = 0; i < 2; ++i) {
        const sketch::Vec2 center = s.point(arcs[i]->points[0])->pos;
        EXPECT_NEAR(center.x, centers[i].x, kTolerance);
        EXPECT_NEAR(center.y, centers[i].y, kTolerance);
        EXPECT_NEAR(arcs[i]->radius, width / 2.0, kTolerance);
        for (int tip = 1; tip <= 2; ++tip) {
            EXPECT_NEAR(std::abs(s.point(arcs[i]->points[tip])->pos.y - center.y), width / 2.0, kTolerance);
        }
    }
}

int changedPixels(UiDriver& ui, const QImage& before, const QImage& after, const QRectF& region, QPointF cursor)
{
    const double scale = static_cast<double>(after.width()) / ui.canvas()->width();
    int changed = 0;
    for (int y = static_cast<int>(region.top() * scale); y < static_cast<int>(region.bottom() * scale); ++y) {
        for (int x = static_cast<int>(region.left() * scale); x < static_cast<int>(region.right() * scale); ++x) {
            if (std::abs(x / scale - cursor.x()) < kCursorClearPixels || !after.rect().contains(x, y)) {
                continue;
            }
            changed += before.pixel(x, y) != after.pixel(x, y) ? 1 : 0;
        }
    }
    return changed;
}

TEST_F(SlotTest, PreviewsThenKeepsItsShapeWhenTheWidthChanges)
{
    startSketch(*ui, "Slot");
    ui->click(gp_Pnt(-20, 20, 0));
    ui->click(gp_Pnt(20, 20, 0));
    ui->move(ui->at(gp_Pnt(0, 20, 0)));
    const QImage flat = ui->canvasImage();
    ui->move(ui->at(gp_Pnt(0, 30, 0)));
    ui->save("slot preview");
    const QImage wide = ui->canvasImage();
    const QRectF above = QRectF(ui->at(gp_Pnt(-25, 33, 0)), ui->at(gp_Pnt(25, 23, 0))).normalized();
    EXPECT_GE(changedPixels(*ui, flat, wide, above, ui->at(gp_Pnt(0, 30, 0))), kPreviewPixels)
        << "The preview draws the slot outline";

    ui->click(gp_Pnt(0, 30, 0));
    ui->save("slot");
    const sketch::Sketch* s = lastSketch(*ui);
    ASSERT_NE(s, nullptr);
    expectSlot(*s, {-20, 20}, {20, 20}, 20.0);
    const nlohmann::json dimensions = ui->state()["dimensions"];
    ASSERT_EQ(dimensions.size(), 1U) << dimensions.dump(2);
    EXPECT_NEAR(dimensions[0]["value"].get<double>(), 20.0, kTolerance);

    editDimension(*ui, dimensions[0]["id"].get<sketch::Id>(), "16");
    ui->save("wider slot");
    s = lastSketch(*ui);
    const std::vector<const sketch::Curve*> arcs = arcsOf(*s);
    ASSERT_EQ(arcs.size(), 2U);
    const sketch::Vec2 a = s->point(arcs[0]->points[0])->pos;
    const sketch::Vec2 b = s->point(arcs[1]->points[0])->pos;
    expectSlot(*s, a, b, 16.0);
    EXPECT_NEAR(a.y, b.y, kTolerance) << "The sides stay parallel to the centers";
}

TEST_F(SlotTest, TypedLengthAndWidthBecomeDimensions)
{
    startSketch(*ui, "Slot");
    ui->click(gp_Pnt(-20, 20, 0));
    ui->move(ui->at(gp_Pnt(0, 20, 0)));
    ui->type("30");
    ui->key(Qt::Key_Tab);
    ui->type("8");
    ui->save("typed slot");
    ui->key(Qt::Key_Return);
    const sketch::Sketch* s = lastSketch(*ui);
    ASSERT_NE(s, nullptr);
    expectSlot(*s, {-20, 20}, {10, 20}, 8.0);
    std::vector<double> values;
    const nlohmann::json dimensions = ui->state()["dimensions"];
    for (const nlohmann::json& dimension : dimensions) {
        values.push_back(dimension["value"].get<double>());
    }
    std::ranges::sort(values);
    ASSERT_EQ(values.size(), 2U) << ui->state().dump(2);
    EXPECT_NEAR(values[0], 8.0, kTolerance);
    EXPECT_NEAR(values[1], 30.0, kTolerance);
}

TEST_F(SlotTest, CenterPointAndOverallVariantsPlaceTheArcCenters)
{
    startSketch(*ui, "Center Point Slot");
    ui->click(gp_Pnt(0, 0, 0));
    glide(*ui, gp_Pnt(0, 0, 0), gp_Pnt(20, 0, 0));
    ui->click(gp_Pnt(20, 0, 0));
    glide(*ui, gp_Pnt(20, 0, 0), gp_Pnt(8, 10, 0));
    ui->click(gp_Pnt(8, 10, 0));
    const sketch::Sketch* s = lastSketch(*ui);
    ASSERT_NE(s, nullptr);
    expectSlot(*s, {-20, 0}, {20, 0}, 20.0);
    const bool centered = std::ranges::any_of(s->constraints(), [](const auto& entry) {
        return entry.second.kind == sketch::ConstraintKind::Midpoint && entry.second.refs[0] == sketch::kOriginId;
    });
    EXPECT_TRUE(centered) << "The clicked center stays in the middle";

    ui->trigger("Overall Slot");
    ui->click(gp_Pnt(-30, 40, 0));
    ui->click(gp_Pnt(30, 40, 0));
    ui->click(gp_Pnt(0, 50, 0));
    ui->save("center point and overall slots");
    s = lastSketch(*ui);
    std::vector<const sketch::Curve*> upper;
    for (const auto& [id, curve] : s->curves()) {
        if (curve.kind == sketch::CurveKind::Arc && s->point(curve.points[0])->pos.y > 15.0) {
            upper.push_back(&curve);
        }
    }
    ASSERT_EQ(upper.size(), 2U);
    for (const sketch::Curve* arc : upper) {
        EXPECT_NEAR(std::abs(s->point(arc->points[0])->pos.x), 20.0, kTolerance) << "Ends sit at the clicked points";
        EXPECT_NEAR(arc->radius, 10.0, kTolerance);
    }
}

} // namespace
} // namespace kalve137::uitest
