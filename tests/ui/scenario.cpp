// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/sketch/sketch_editor.h"
#include "core/feature.h"
#include "sketch/dimension_layout.h"
#include "sketch/sketch.h"
#include "viewer/occt_view_widget.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <variant>

namespace kalve137::uitest {

namespace {

constexpr double kGlidePixels = 2.0;
constexpr double kProbeLength = 10.0;

} // namespace

core::Document blockDocument()
{
    core::Document document;
    core::SketchFeature base;
    sketch::Sketch& s = base.sketch;
    const auto a = s.addPoint({-kBlockWidth / 2, -kBlockDepth / 2});
    const auto b = s.addPoint({kBlockWidth / 2, -kBlockDepth / 2});
    const auto c = s.addPoint({kBlockWidth / 2, kBlockDepth / 2});
    const auto d = s.addPoint({-kBlockWidth / 2, kBlockDepth / 2});
    s.addConstraint(sketch::ConstraintKind::Horizontal, {s.addLine(a, b)});
    s.addConstraint(sketch::ConstraintKind::Vertical, {s.addLine(b, c)});
    s.addConstraint(sketch::ConstraintKind::Horizontal, {s.addLine(c, d)});
    s.addConstraint(sketch::ConstraintKind::Vertical, {s.addLine(d, a)});
    core::ExtrudeFeature pad;
    pad.profiles = {document.add(std::move(base)), {{0.0, 0.0}}};
    pad.distance = kBlockHeight;
    pad.bodyName = document.takeBodyName();
    document.add(std::move(pad));
    return document;
}

void sketchRectangle(UiDriver& ui, core::OriginPlane plane, const gp_Pnt& corner, const gp_Pnt& opposite)
{
    ui.trigger("Create Sketch");
    ui.click(ui.originPlane(plane));
    ASSERT_NE(ui.state()["sketch"], 0) << ui.state().dump(2);
    ui.trigger("Rectangle");
    ui.click(corner);
    ui.click(opposite);
    ui.trigger("Finish Sketch");
}

void extrude(UiDriver& ui, const QString& distance)
{
    ui.trigger("Extrude");
    ASSERT_TRUE(ui.state()["featurePanel"]) << ui.state().dump(2);
    ui.fill("Distance", distance);
    ASSERT_FALSE(ui.state()["featurePanel"]) << ui.state().dump(2);
}

void block(UiDriver& ui)
{
    sketchRectangle(ui, core::OriginPlane::XY, gp_Pnt(-kBlockWidth / 2, -kBlockDepth / 2, 0),
                    gp_Pnt(kBlockWidth / 2, kBlockDepth / 2, 0));
    extrude(ui, QString::number(kBlockHeight));
    ASSERT_EQ(ui.state()["bodies"].size(), 1U) << ui.state().dump(2);
    homeView(ui);
}

void homeView(UiDriver& ui)
{
    if (ui.viewport() != nullptr) {
        ui.trigger("Home");
        ui.trigger("Fit All");
        return;
    }
    ui.view().homeView();
    ui.view().fitAll();
    ui.settle();
}

double volume(const UiDriver& ui, std::size_t body)
{
    const nlohmann::json state = ui.state();
    return body < state["bodies"].size() ? state["bodies"][body]["volume"].get<double>() : 0.0;
}

int faces(const UiDriver& ui, std::size_t body)
{
    const nlohmann::json state = ui.state();
    return body < state["bodies"].size() ? state["bodies"][body]["faces"].get<int>() : 0;
}

void glide(UiDriver& ui, const gp_Pnt& from, const gp_Pnt& to)
{
    const QPointF span = ui.at(to) - ui.at(from);
    const int steps = std::max(1, static_cast<int>(std::ceil(std::hypot(span.x(), span.y()) / kGlidePixels)));
    for (int step = 1; step <= steps; ++step) {
        const double t = static_cast<double>(step) / steps;
        ui.move(ui.at(gp_Pnt(from.XYZ() + (to.XYZ() - from.XYZ()) * t)));
    }
}

void startSketch(UiDriver& ui, const QString& tool)
{
    ui.trigger("Create Sketch");
    ui.click(ui.originPlane(core::OriginPlane::XY));
    ASSERT_NE(ui.state()["sketch"], 0) << ui.state().dump(2);
    ui.trigger(tool);
}

const sketch::Sketch* lastSketch(const UiDriver& ui)
{
    const auto* feature = std::get_if<core::SketchFeature>(&ui.document().features().back().data);
    return feature != nullptr ? &feature->sketch : nullptr;
}

std::optional<QPointF> dimensionLabel(UiDriver& ui, sketch::Id id)
{
    const sketch::Sketch* s = lastSketch(ui);
    if (s == nullptr || s->constraint(id) == nullptr) {
        return std::nullopt;
    }
    const QPointF origin = ui.at(gp_Pnt(0, 0, 0));
    const QPointF probe = ui.at(gp_Pnt(kProbeLength, 0, 0));
    const double wpp = kProbeLength / std::hypot(probe.x() - origin.x(), probe.y() - origin.y());
    const std::optional<sketch::DimensionLayout> layout =
        sketch::layoutDimension(*s, *s->constraint(id), wpp, sketch::dimensionMiddle(*s));
    if (!layout) {
        return std::nullopt;
    }
    return ui.at(gp_Pnt(layout->text.x, layout->text.y, 0));
}

void editDimension(UiDriver& ui, sketch::Id id, const QString& value)
{
    const std::optional<QPointF> found = dimensionLabel(ui, id);
    ASSERT_TRUE(found.has_value());
    const QPointF label = *found;
    ui.trigger("Select");
    const auto* editor = ui.window().findChild<app::SketchEditor*>();
    ASSERT_NE(editor, nullptr);
    const app::SketchEditor::Hit hit = editor->hitAt(label);
    ASSERT_EQ(hit.kind, app::SketchEditor::Hit::Kind::Constraint);
    ASSERT_EQ(hit.id, id);
    ui.doubleClick(label);
    ui.type(value);
    ui.key(Qt::Key_Return);
}

} // namespace kalve137::uitest
