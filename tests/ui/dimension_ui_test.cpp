// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include <QAction>
#include <QLineEdit>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <gtest/gtest.h>

namespace kalve137::uitest {
namespace {

using DimensionTest = UiTest;

constexpr double kNear = 1e-6;
constexpr double kPixel = 0.1;
constexpr double kLabelHalfWidth = 16.0;
constexpr double kEditorSlack = 2.0;

void openRectangle(UiDriver& ui)
{
    sketchRectangle(ui, core::OriginPlane::XY, gp_Pnt(-20, -10, 0), gp_Pnt(20, 10, 0));
    ui.doubleClick(ui.timelineChip(1));
    ASSERT_EQ(ui.state()["sketch"], 1) << ui.state().dump(2);
    ui.trigger("Dimension");
}

TEST_F(DimensionTest, LandsWhereTheClickPutsItAndPreviewsTheTypedValue)
{
    openRectangle(*ui);
    ui->click(gp_Pnt(0, -10, 0));
    ui->move(ui->at(gp_Pnt(10, -13, 0)));
    ui->save("following the cursor");
    EXPECT_TRUE(ui->state()["dimensions"].empty()) << "Nothing is added before the placing click";

    ui->click(gp_Pnt(10, -13, 0));
    nlohmann::json state = ui->state();
    ASSERT_EQ(state["dimensions"].size(), 1U) << state.dump(2);
    const nlohmann::json placed = state["dimensions"][0];
    ASSERT_TRUE(placed["label"].is_array()) << placed.dump();
    EXPECT_NEAR(placed["label"][0].get<double>(), 10.0, kPixel);
    EXPECT_NEAR(placed["label"][1].get<double>(), -13.0, kPixel);

    ui->type("56");
    ui->save("typed value preview");
    state = ui->state();
    ASSERT_EQ(state["dimensions"].size(), 1U) << state.dump(2);
    EXPECT_NEAR(state["dimensions"][0]["measured"].get<double>(), 56.0, 1e-4) << state.dump(2);

    ui->key(Qt::Key_Return);
    state = ui->state();
    ASSERT_EQ(state["dimensions"].size(), 1U) << state.dump(2);
    const nlohmann::json kept = state["dimensions"][0];
    EXPECT_NEAR(kept["value"].get<double>(), 56.0, kNear);
    EXPECT_NEAR(kept["measured"].get<double>(), 56.0, 1e-4);
    EXPECT_NEAR(kept["label"][1].get<double>(), -13.0, kPixel) << "The label keeps its offset from the line";
}

TEST_F(DimensionTest, TwoPointsMeasureAlongTheAxisTheCursorLeavesBy)
{
    openRectangle(*ui);
    ui->click(gp_Pnt(-20, 10, 0));
    ui->click(gp_Pnt(20, -10, 0));
    ui->move(ui->at(gp_Pnt(0, 13, 0)));
    ui->save("horizontal from above");
    ui->click(gp_Pnt(0, 13, 0));
    const nlohmann::json state = ui->state();
    ASSERT_EQ(state["dimensions"].size(), 1U) << state.dump(2);
    EXPECT_EQ(state["dimensions"][0]["kind"], "horizontal_distance") << state.dump(2);
    EXPECT_NEAR(state["dimensions"][0]["value"].get<double>(), 40.0, kNear);
    ui->key(Qt::Key_Return);
    EXPECT_EQ(ui->state()["dimensions"][0]["kind"], "horizontal_distance");
}

void expectInPlaceOfLabel(UiDriver& ui, QPointF label)
{
    const QWidget* editor = ui.focused();
    ASSERT_NE(editor, nullptr);
    ASSERT_EQ(editor->objectName(), "dimensionEditor");
    const QPointF center = QRectF(editor->geometry()).center();
    EXPECT_NEAR(center.x(), label.x(), kEditorSlack) << "Editor replaces label";
    EXPECT_NEAR(center.y(), label.y(), kEditorSlack) << "Editor replaces label";
    EXPECT_GE(editor->width(), 2 * kLabelHalfWidth) << "Editor covers label pill";
    const auto* line = qobject_cast<const QLineEdit*>(editor);
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(line->alignment() & Qt::AlignHCenter) << "Value sits where label text was";
}

TEST_F(DimensionTest, ValueEditorTakesThePlaceOfTheLabelItEdits)
{
    openRectangle(*ui);
    ui->click(gp_Pnt(0, -10, 0));
    ui->click(gp_Pnt(10, -13, 0));
    ui->save("editor on a new label");
    expectInPlaceOfLabel(*ui, ui->at(gp_Pnt(10, -13, 0)));

    ui->key(Qt::Key_Return);
    ui->click(gp_Pnt(10, -13, 0));
    ui->save("editor on an edited label");
    expectInPlaceOfLabel(*ui, ui->at(gp_Pnt(10, -13, 0)));
}

TEST_F(DimensionTest, DraggingALabelMovesItWithoutChangingTheValue)
{
    openRectangle(*ui);
    ui->click(gp_Pnt(0, -10, 0));
    ui->click(gp_Pnt(10, -13, 0));
    ui->key(Qt::Key_Return);
    ui->key(Qt::Key_Escape);
    const nlohmann::json before = ui->state()["dimensions"][0];

    ui->move(ui->at(gp_Pnt(10, -13, 0)));
    ui->drag(ui->at(gp_Pnt(10, -13, 0)), ui->at(gp_Pnt(-5, -18, 0)));
    nlohmann::json moved = ui->state()["dimensions"][0];
    EXPECT_NEAR(moved["label"][0].get<double>(), -5.0, kPixel) << moved.dump();
    EXPECT_NEAR(moved["label"][1].get<double>(), -18.0, kPixel) << moved.dump();
    EXPECT_NEAR(moved["value"].get<double>(), before["value"].get<double>(), kNear);
    EXPECT_EQ(ui->action("Undo")->text(), QStringLiteral("Undo Move Dimension"));

    const QPointF from = ui->at(gp_Pnt(-5, -18, 0));
    ui->move(from);
    ui->press(from);
    ui->move(ui->at(gp_Pnt(15, -25, 0)), Qt::LeftButton);
    ui->key(Qt::Key_Escape);
    ui->release(ui->at(gp_Pnt(15, -25, 0)));
    ui->settle();
    moved = ui->state()["dimensions"][0];
    EXPECT_NEAR(moved["label"][0].get<double>(), -5.0, kPixel) << "Escape puts label back";
    EXPECT_NEAR(moved["label"][1].get<double>(), -18.0, kPixel) << "Escape puts label back";
    EXPECT_EQ(ui->state()["sketch"], 1) << "Escape ends drag before it leaves sketch";
}

TEST_F(DimensionTest, EscapeDropsTheUnplacedDimension)
{
    openRectangle(*ui);
    ui->click(gp_Pnt(0, -10, 0));
    ui->click(gp_Pnt(-10, -13, 0));
    ASSERT_EQ(ui->state()["dimensions"].size(), 1U) << ui->state().dump(2);
    ui->type("70");
    ui->key(Qt::Key_Escape);
    const nlohmann::json state = ui->state();
    EXPECT_TRUE(state["dimensions"].empty()) << state.dump(2);
    EXPECT_EQ(ui->state()["sketch"], 1) << "Esc closes the value editor before it leaves the sketch";
}

} // namespace
} // namespace kalve137::uitest
