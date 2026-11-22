// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include "core/feature.h"
#include "sketch/sketch.h"

#include <QAction>

#include <gtest/gtest.h>

#include <map>
#include <variant>

namespace kalve137::uitest {
namespace {

using SketchMoveTest = UiTest;

constexpr double kRigid = 1e-6;
constexpr double kPixel = 0.25;

const sketch::Sketch& sketchOf(const UiDriver& ui)
{
    const auto* feature = std::get_if<core::SketchFeature>(&ui.document().features().front().data);
    EXPECT_NE(feature, nullptr);
    static const sketch::Sketch kEmpty;
    return feature != nullptr ? feature->sketch : kEmpty;
}

std::map<sketch::Id, sketch::Vec2> positions(const UiDriver& ui)
{
    std::map<sketch::Id, sketch::Vec2> result;
    for (const auto& [id, point] : sketchOf(ui).points()) {
        if (id != sketch::kOriginId) {
            result.emplace(id, point.pos);
        }
    }
    return result;
}

void expectShiftedBy(const std::map<sketch::Id, sketch::Vec2>& before, const std::map<sketch::Id, sketch::Vec2>& after,
                     sketch::Vec2 delta)
{
    ASSERT_EQ(before.size(), after.size());
    ASSERT_FALSE(before.empty());
    const sketch::Vec2 first = after.begin()->second - before.begin()->second;
    EXPECT_NEAR(first.x, delta.x, kPixel);
    EXPECT_NEAR(first.y, delta.y, kPixel);
    for (const auto& [id, pos] : before) {
        const sketch::Vec2 moved = after.at(id) - pos;
        EXPECT_NEAR(moved.x, first.x, kRigid) << "Point " << id << " moved with the rest";
        EXPECT_NEAR(moved.y, first.y, kRigid) << "Point " << id << " moved with the rest";
    }
}

void drag(UiDriver& ui, const gp_Pnt& from, const gp_Pnt& to)
{
    ui.press(ui.at(from));
    ui.move(ui.at(gp_Pnt((from.X() + to.X()) / 2, (from.Y() + to.Y()) / 2, 0)), Qt::LeftButton);
    ui.move(ui.at(to), Qt::LeftButton);
    ui.release(ui.at(to));
}

void openSketch(UiDriver& ui)
{
    ui.trigger("Create Sketch");
    ui.click(ui.originPlane(core::OriginPlane::XY));
    ASSERT_NE(ui.state()["sketch"], 0) << ui.state().dump(2);
}

TEST_F(SketchMoveTest, LineDragsAsAWhole)
{
    openSketch(*ui);
    ui->trigger("Line");
    ui->click(gp_Pnt(-10, 0, 0));
    ui->click(gp_Pnt(10, 6, 0));
    ui->key(Qt::Key_Escape);
    ui->trigger("Select");
    const auto before = positions(*ui);
    ASSERT_EQ(before.size(), 2U);

    drag(*ui, gp_Pnt(0, 3, 0), gp_Pnt(5, 8, 0));
    ui->save("line dragged");
    expectShiftedBy(before, positions(*ui), {5, 5});
    EXPECT_EQ(ui->action("Undo")->text(), QStringLiteral("Undo Move"));
}

TEST_F(SketchMoveTest, BoxSelectionDragsTogether)
{
    openSketch(*ui);
    ui->trigger("Rectangle");
    ui->click(gp_Pnt(-10, -5, 0));
    ui->click(gp_Pnt(10, 5, 0));
    ui->trigger("Circle");
    ui->click(gp_Pnt(20, 0, 0));
    ui->click(gp_Pnt(23, 0, 0));
    ui->trigger("Select");
    const auto before = positions(*ui);

    drag(*ui, gp_Pnt(-15, -10, 0), gp_Pnt(30, 10, 0));
    ASSERT_FALSE(ui->selectionText().isEmpty());
    drag(*ui, gp_Pnt(0, 5, 0), gp_Pnt(-4, 11, 0));
    ui->save("box selection dragged");
    expectShiftedBy(before, positions(*ui), {-4, 6});
}

TEST_F(SketchMoveTest, TypedOffsetsMoveTheSelection)
{
    openSketch(*ui);
    ui->trigger("Circle");
    ui->click(gp_Pnt(20, 5, 0));
    ui->click(gp_Pnt(25, 5, 0));
    ui->trigger("Line");
    ui->click(gp_Pnt(-10, -10, 0));
    ui->click(gp_Pnt(-10, 10, 0));
    ui->key(Qt::Key_Escape);
    ui->trigger("Select");
    ui->click(gp_Pnt(25, 5, 0));
    ui->click(gp_Pnt(-10, 0, 0), Qt::ShiftModifier);
    const auto before = positions(*ui);
    ASSERT_EQ(before.size(), 3U) << "Circle center and both line ends";

    ui->trigger("Move");
    ASSERT_NE(ui->focused(), nullptr);
    EXPECT_EQ(ui->focused()->objectName(), "sketchInput");
    ui->type("12.5");
    ui->key(Qt::Key_Tab);
    ui->type("-4");
    ui->save("typed offsets");
    ui->key(Qt::Key_Return);
    const auto after = positions(*ui);
    for (const auto& [id, pos] : before) {
        EXPECT_NEAR(after.at(id).x - pos.x, 12.5, kRigid);
        EXPECT_NEAR(after.at(id).y - pos.y, -4.0, kRigid);
    }
    EXPECT_EQ(ui->action("Undo")->text(), QStringLiteral("Undo Move"));

    ui->trigger("Move");
    ui->type("30");
    ui->key(Qt::Key_Escape);
    for (const auto& [id, pos] : positions(*ui)) {
        EXPECT_EQ(pos.x, after.at(id).x) << "Esc leaves the geometry where it was";
        EXPECT_EQ(pos.y, after.at(id).y);
    }
}

} // namespace
} // namespace kalve137::uitest
