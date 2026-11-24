// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"
#include "core/document.h"
#include "core/feature.h"
#include "scenario.h"
#include "sketch/dimension.h"
#include "sketch/dimension_layout.h"
#include "sketch/sketch.h"
#include "ui_test.h"
#include "viewer/occt_view_widget.h"

#include <AIS_InteractiveObject.hxx>
#include <NCollection_List.hxx>
#include <Prs3d_ArrowAspect.hxx>
#include <Prs3d_DimensionAspect.hxx>
#include <PrsDim_AngleDimension.hxx>
#include <PrsDim_DiameterDimension.hxx>
#include <PrsDim_LengthDimension.hxx>
#include <PrsDim_RadiusDimension.hxx>
#include <gp_Pnt.hxx>

#include <QAction>
#include <QLineEdit>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

using DimensionTest = UiTest;

constexpr double kNear = 1e-6;
constexpr double kPixel = 0.1;
constexpr double kLabelHalfWidth = 16.0;
constexpr double kEditorSlack = 2.0;
constexpr double kLayoutPixels = 1.0;
constexpr int kRescaleSettleMs = 300;

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

enum class Shape { Length, Radius, Diameter, Angle };

struct Shown {
    Handle(PrsDim_Dimension) dimension;
    Shape shape = Shape::Length;
    std::vector<sketch::Vec2> keys;
};

sketch::Vec2 flat(const gp_Pnt& p)
{
    return {p.X(), p.Y()};
}

std::vector<Shown> shownDimensions(UiDriver& ui)
{
    NCollection_List<Handle(AIS_InteractiveObject)> displayed;
    ui.view().aisContext()->DisplayedObjects(displayed);
    std::vector<Shown> shown;
    for (const Handle(AIS_InteractiveObject)& object : displayed) {
        if (const auto* length = dynamic_cast<PrsDim_LengthDimension*>(object.get())) {
            shown.push_back({length, Shape::Length, {flat(length->FirstPoint()), flat(length->SecondPoint())}});
        } else if (const auto* radius = dynamic_cast<PrsDim_RadiusDimension*>(object.get())) {
            shown.push_back({radius, Shape::Radius, {flat(radius->Circle().Location())}});
        } else if (const auto* diameter = dynamic_cast<PrsDim_DiameterDimension*>(object.get())) {
            shown.push_back({diameter, Shape::Diameter, {flat(diameter->Circle().Location())}});
        } else if (const auto* angle = dynamic_cast<PrsDim_AngleDimension*>(object.get())) {
            shown.push_back({angle, Shape::Angle, {flat(angle->CenterPoint())}});
        }
    }
    return shown;
}

std::optional<Shown> expectedOf(const sketch::Sketch& s, const sketch::Constraint& constraint)
{
    const auto first = [&] { return s.point(s.curve(constraint.refs[0])->points[0])->pos; };
    using sketch::ConstraintKind;
    switch (constraint.kind) {
    case ConstraintKind::Distance:
    case ConstraintKind::HorizontalDistance:
    case ConstraintKind::VerticalDistance: {
        const auto ends = sketch::measuredPoints(s, constraint);
        if (!ends) {
            return std::nullopt;
        }
        return Shown{.dimension = {}, .shape = Shape::Length, .keys = {ends->first, ends->second}};
    }
    case ConstraintKind::Radius:
    case ConstraintKind::Diameter:
        return Shown{.dimension = {},
                     .shape = constraint.kind == ConstraintKind::Radius ? Shape::Radius : Shape::Diameter,
                     .keys = {first()}};
    case ConstraintKind::Angle:
        return Shown{.dimension = {}, .shape = Shape::Angle, .keys = {first()}};
    default:
        return std::nullopt;
    }
}

bool sameAs(const Shown& item, const Shown& expected)
{
    return item.shape == expected.shape &&
           std::ranges::equal(item.keys, expected.keys,
                              [](sketch::Vec2 p, sketch::Vec2 q) { return sketch::distance(p, q) < kNear; });
}

TEST_F(DimensionTest, LayoutPutsTextAndArrowsWhereTheOldViewDoes)
{
    if (ui->viewport() != nullptr) {
        GTEST_SKIP() << "Reads the old view's dimensions";
    }
    core::Document document;
    core::SketchFeature feature;
    sketch::Sketch& s = feature.sketch;
    using sketch::ConstraintKind;
    const sketch::Id a = s.addPoint({0.0, 0.0});
    const sketch::Id b = s.addPoint({40.0, 0.0});
    const sketch::Id c = s.addPoint({40.0, 20.0});
    const sketch::Id d = s.addPoint({0.0, 20.0});
    const sketch::Id bottom = s.addLine(a, b);
    s.addLine(b, c);
    const sketch::Id top = s.addLine(c, d);
    const sketch::Id left = s.addLine(d, a);
    s.addConstraint(ConstraintKind::Distance, {bottom}, 40.0);
    s.addConstraint(ConstraintKind::Distance, {top}, 40.0);
    s.constraint(s.constraints().rbegin()->first)->labelOffset = sketch::Vec2{6.0, 9.0};
    s.addConstraint(ConstraintKind::Distance, {left}, 20.0);
    s.constraint(s.constraints().rbegin()->first)->labelOffset = sketch::Vec2{-7.0, 18.0};
    s.addConstraint(ConstraintKind::HorizontalDistance, {d, b}, 40.0);
    s.addConstraint(ConstraintKind::VerticalDistance, {a, c}, 20.0);
    const sketch::Id hole = s.addCircle(s.addPoint({60.0, 10.0}), 6.0);
    s.addConstraint(ConstraintKind::Radius, {hole}, 6.0);
    const sketch::Id pin = s.addCircle(s.addPoint({60.0, -20.0}), 4.0);
    s.addConstraint(ConstraintKind::Radius, {pin}, 4.0);
    s.constraint(s.constraints().rbegin()->first)->labelOffset = sketch::Vec2{9.0, -3.0};
    const sketch::Id bore = s.addCircle(s.addPoint({-25.0, 10.0}), 8.0);
    s.addConstraint(ConstraintKind::Diameter, {bore}, 16.0);
    const sketch::Id corner = s.addPoint({80.0, 0.0});
    const sketch::Id flat1 = s.addLine(corner, s.addPoint({110.0, 0.0}));
    const sketch::Id slant1 = s.addLine(corner, s.addPoint({100.0, 15.0}));
    s.addConstraint(ConstraintKind::Angle, {flat1, slant1}, 36.869898);
    const sketch::Id corner2 = s.addPoint({80.0, -30.0});
    const sketch::Id flat2 = s.addLine(corner2, s.addPoint({110.0, -30.0}));
    const sketch::Id slant2 = s.addLine(corner2, s.addPoint({95.0, -10.0}));
    s.addConstraint(ConstraintKind::Angle, {flat2, slant2}, 53.130102);
    s.constraint(s.constraints().rbegin()->first)->labelOffset = sketch::Vec2{14.0, 6.0};
    const sketch::Id corner3 = s.addPoint({80.0, -60.0});
    const sketch::Id flat3 = s.addLine(corner3, s.addPoint({110.0, -60.0}));
    const sketch::Id slant3 = s.addLine(corner3, s.addPoint({95.0, -40.0}));
    s.addConstraint(ConstraintKind::Angle, {flat3, slant3}, 53.130102);
    s.constraint(s.constraints().rbegin()->first)->labelOffset = sketch::Vec2{20.0, -4.0};
    const sketch::Sketch drawn = s;
    const core::FeatureId id = document.add(std::move(feature));
    ui->session().reset(std::move(document), {});
    homeView(*ui);
    ui->doubleClick(ui->timelineChip(id));
    ASSERT_EQ(ui->state()["sketch"], id) << ui->state().dump(2);
    ui->settle(kRescaleSettleMs);

    const double wpp = ui->view().worldPerPixel();
    const std::vector<Shown> shown = shownDimensions(*ui);
    const std::map<sketch::Id, sketch::DimensionLayout> layouts = sketch::layoutDimensions(drawn, wpp);
    std::map<ConstraintKind, std::pair<double, double>> worst;
    std::ofstream record(ui->save("dimension layout").parent_path() / "dimension-layout.tsv");
    record << "constraint\tkind\tlabeled\ttext_px\tarrow_px\n";
    for (const auto& [cid, constraint] : drawn.constraints()) {
        if (!sketch::isDimension(constraint.kind)) {
            continue;
        }
        const auto layout = layouts.find(cid);
        ASSERT_NE(layout, layouts.end()) << cid;
        const std::optional<Shown> expected = expectedOf(drawn, constraint);
        ASSERT_TRUE(expected.has_value()) << cid;
        const auto old = std::ranges::find_if(shown, [&](const Shown& item) { return sameAs(item, *expected); });
        ASSERT_NE(old, shown.end()) << "Old view shows constraint " << cid;
        const double text = sketch::distance(layout->second.text, flat(old->dimension->GetTextPosition())) / wpp;
        const sketch::DimensionArrow& arrow = layout->second.arrows.front();
        const double arrowLength = sketch::distance(arrow.tip, (arrow.left + arrow.right) * 0.5);
        const double arrowDelta =
            std::abs(arrowLength - old->dimension->DimensionAspect()->ArrowAspect()->Length()) / wpp;
        const bool labeled = constraint.labelOffset.has_value();
        record << cid << '\t' << sketch::toString(constraint.kind) << '\t' << labeled << '\t' << text << '\t'
               << arrowDelta << '\n';
        if (constraint.kind == ConstraintKind::Angle && !labeled) {
            continue;
        }
        auto& [textWorst, arrowWorst] = worst[constraint.kind];
        textWorst = std::max(textWorst, text);
        arrowWorst = std::max(arrowWorst, arrowDelta);
    }
    EXPECT_EQ(worst.size(), 6U);
    for (const auto& [kind, deltas] : worst) {
        EXPECT_LE(deltas.first, kLayoutPixels) << sketch::toString(kind) << " text";
        EXPECT_LE(deltas.second, kLayoutPixels) << sketch::toString(kind) << " arrow";
    }
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
