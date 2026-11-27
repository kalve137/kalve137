// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include "app/shell/main_window.h"
#include "app/sketch/badge_placement.h"
#include "app/sketch/sketch_editor.h"
#include "sketch/sketch.h"

#include <QPointF>
#include <QString>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace kalve137::uitest {
namespace {

using OffsetTest = UiTest;

constexpr double kTolerance = 1e-4;

std::vector<double> horizontalLevels(const sketch::Sketch& s)
{
    std::vector<double> levels;
    for (const auto& [id, curve] : s.curves()) {
        if (curve.kind != sketch::CurveKind::Line || curve.construction) {
            continue;
        }
        const sketch::Vec2 a = s.point(curve.points[0])->pos;
        const sketch::Vec2 b = s.point(curve.points[1])->pos;
        if (std::abs(a.y - b.y) < kTolerance) {
            levels.push_back(a.y);
        }
    }
    std::ranges::sort(levels);
    return levels;
}

void expectLevels(const sketch::Sketch& s, const std::vector<double>& expected)
{
    const std::vector<double> levels = horizontalLevels(s);
    ASSERT_EQ(levels.size(), expected.size());
    for (std::size_t i = 0; i < levels.size(); ++i) {
        EXPECT_NEAR(levels[i], expected[i], kTolerance);
    }
}

void expectOneBadgeAndDimensionPerOffset(UiDriver& ui, const sketch::Sketch& s, std::size_t offsets)
{
    std::vector<sketch::Id> ids;
    for (const auto& [id, constraint] : s.constraints()) {
        EXPECT_NE(constraint.kind, sketch::ConstraintKind::Equal) << id;
        if (constraint.kind == sketch::ConstraintKind::Offset) {
            ids.push_back(id);
        }
    }
    for (const auto& [id, curve] : s.curves()) {
        EXPECT_FALSE(curve.construction) << id;
    }
    ASSERT_EQ(ids.size(), offsets);
    const std::vector<app::BadgePlace> badges = app::placeBadges(s);
    const nlohmann::json dimensions = ui.state()["dimensions"];
    EXPECT_EQ(dimensions.size(), offsets) << dimensions.dump(2);
    for (const sketch::Id id : ids) {
        EXPECT_EQ(std::ranges::count(badges, id, &app::BadgePlace::constraint), 1) << id;
        EXPECT_EQ(std::ranges::count_if(dimensions, [&](const nlohmann::json& d) { return d["id"] == id; }), 1) << id;
    }
}

sketch::Id dimensionWithValue(UiDriver& ui, double value)
{
    const nlohmann::json dimensions = ui.state()["dimensions"];
    for (const nlohmann::json& dimension : dimensions) {
        if (std::abs(dimension["value"].get<double>() - value) < kTolerance) {
            return dimension["id"].get<sketch::Id>();
        }
    }
    return 0;
}

TEST_F(OffsetTest, OffsetsALoopOnTheCursorSideAndFollowsItsDistance)
{
    startSketch(*ui, "Rectangle");
    ui->click(gp_Pnt(-20, -10, 0));
    ui->click(gp_Pnt(20, 10, 0));

    ui->trigger("Offset");
    ui->click(gp_Pnt(0, -10, 0));
    glide(*ui, gp_Pnt(0, -10, 0), gp_Pnt(0, -16, 0));
    ui->type("5");
    ui->save("offset preview");
    const auto* editor = ui->window().findChild<app::SketchEditor*>();
    ASSERT_NE(editor, nullptr);
    expectLevels(editor->shownSketch(), {-15, -10, 10, 15});
    ui->key(Qt::Key_Return);
    const sketch::Sketch* s = lastSketch(*ui);
    ASSERT_NE(s, nullptr);
    expectLevels(*s, {-15, -10, 10, 15});
    expectOneBadgeAndDimensionPerOffset(*ui, *s, 1);

    ui->trigger("Offset");
    ui->click(gp_Pnt(0, -10, 0));
    glide(*ui, gp_Pnt(0, -10, 0), gp_Pnt(0, -7, 0));
    ui->type("3");
    ui->key(Qt::Key_Return);
    ui->save("offset both ways");
    expectLevels(*lastSketch(*ui), {-15, -10, -7, 7, 10, 15});
    expectOneBadgeAndDimensionPerOffset(*ui, *lastSketch(*ui), 2);

    const sketch::Id outer = dimensionWithValue(*ui, 5.0);
    ASSERT_NE(outer, 0) << ui->state().dump(2);
    editDimension(*ui, outer, "8");
    ui->save("wider offset");
    expectLevels(*lastSketch(*ui), {-18, -10, -7, 7, 10, 18});
}

TEST_F(OffsetTest, ANegativeDistanceFlipsTheSideAndOneUndoTakesItBack)
{
    startSketch(*ui, "Rectangle");
    ui->click(gp_Pnt(-20, -10, 0));
    ui->click(gp_Pnt(20, 10, 0));
    const std::size_t before = lastSketch(*ui)->curves().size();

    ui->trigger("Offset");
    ui->click(gp_Pnt(0, -10, 0));
    glide(*ui, gp_Pnt(0, -10, 0), gp_Pnt(0, -16, 0));
    ui->type("-3");
    ui->key(Qt::Key_Return);
    ui->save("negative offset");
    expectLevels(*lastSketch(*ui), {-10, -7, 7, 10});
    expectOneBadgeAndDimensionPerOffset(*ui, *lastSketch(*ui), 1);

    EXPECT_EQ(ui->action("Undo")->text(), QStringLiteral("Undo Offset"));
    ui->trigger("Undo Offset");
    EXPECT_EQ(lastSketch(*ui)->curves().size(), before);
    expectLevels(*lastSketch(*ui), {-10, 10});
}

TEST_F(OffsetTest, RefusesADistanceThatDoesntFit)
{
    startSketch(*ui, "Rectangle");
    ui->click(gp_Pnt(-20, -10, 0));
    ui->click(gp_Pnt(20, 10, 0));
    const std::size_t before = lastSketch(*ui)->curves().size();
    const QString undo = ui->action("Undo")->text();

    ui->trigger("Offset");
    ui->click(gp_Pnt(0, -10, 0));
    glide(*ui, gp_Pnt(0, -10, 0), gp_Pnt(0, -7, 0));
    ui->type("12");
    ui->key(Qt::Key_Return);
    EXPECT_EQ(ui->statusText(), QStringLiteral("A 12 mm offset doesn't fit these curves"));
    EXPECT_EQ(lastSketch(*ui)->curves().size(), before);
    EXPECT_TRUE(std::ranges::none_of(lastSketch(*ui)->constraints(), [](const auto& entry) {
        return entry.second.kind == sketch::ConstraintKind::Offset;
    }));
    EXPECT_EQ(ui->action("Undo")->text(), undo);
}

TEST_F(OffsetTest, OffsetsAnOpenChain)
{
    startSketch(*ui, "Line");
    ui->click(gp_Pnt(-20, 0, 0));
    ui->click(gp_Pnt(0, 0, 0));
    ui->click(gp_Pnt(0, 10, 0));
    ui->key(Qt::Key_Escape);
    ASSERT_EQ(lastSketch(*ui)->curves().size(), 2U) << ui->state().dump(2);

    ui->trigger("Offset");
    ui->click(gp_Pnt(-10, 0, 0));
    glide(*ui, gp_Pnt(-10, 0, 0), gp_Pnt(-10, -4, 0));
    ui->type("2");
    ui->key(Qt::Key_Return);
    ui->save("open chain offset");
    const sketch::Sketch* s = lastSketch(*ui);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->curves().size(), 4U);
    expectLevels(*s, {-2, 0});
    expectOneBadgeAndDimensionPerOffset(*ui, *s, 1);
}

TEST_F(OffsetTest, EditingADimensionFromTheOffsetPromptDropsTheChain)
{
    startSketch(*ui, "Rectangle");
    ui->click(gp_Pnt(-20, -10, 0));
    ui->click(gp_Pnt(20, 10, 0));
    ui->trigger("Offset");
    ui->click(gp_Pnt(0, -10, 0));
    glide(*ui, gp_Pnt(0, -10, 0), gp_Pnt(0, -16, 0));
    ui->type("5");
    ui->key(Qt::Key_Return);
    const sketch::Id dimension = dimensionWithValue(*ui, 5.0);
    ASSERT_NE(dimension, 0) << ui->state().dump(2);
    const std::optional<QPointF> label = dimensionLabel(*ui, dimension);
    ASSERT_TRUE(label.has_value());

    ui->trigger("Offset");
    ui->click(gp_Pnt(0, -10, 0));
    const auto* editor = ui->window().findChild<app::SketchEditor*>();
    ASSERT_NE(editor, nullptr);
    const QString picking = editor->hint();
    ui->doubleClick(*label);
    EXPECT_NE(editor->hint(), picking);
    ui->type("7");
    ui->key(Qt::Key_Return);
    expectLevels(*lastSketch(*ui), {-17, -10, 10, 17});
}

} // namespace
} // namespace kalve137::uitest
