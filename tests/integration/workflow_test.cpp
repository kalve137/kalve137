// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"
#include "core/recompute.h"
#include "core/serialization.h"
#include "core/undo_stack.h"
#include "geometry/exchange.h"
#include "geometry/shapes.h"
#include "sketch/solver.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <numbers>

using namespace kalve137;
using namespace kalve137::core;
using sketch::ConstraintKind;

namespace {

constexpr double kWidth = 60.0;
constexpr double kDepth = 30.0;
constexpr double kHeight = 10.0;
constexpr double kHoleDiameter = 10.0;
constexpr double kFilletRadius = 3.0;

double totalVolume(const Model& model)
{
    double total = 0.0;
    for (const Body& body : model.bodies) {
        total += geometry::volume(body.shape);
    }
    return total;
}

double filletLoss(double r, double h)
{
    return (r * r - std::numbers::pi * r * r / 4.0) * h;
}

double holeVolume(double diameter)
{
    return std::numbers::pi * diameter * diameter / 4.0 * kHeight;
}

struct Bracket {
    Document doc;
    FeatureId base = 0;
    sketch::Id widthDimension = 0;
    FeatureId holeSketch = 0;
    sketch::Id holeDimension = 0;
    FeatureId fillet = 0;
};

Bracket buildBracket()
{
    Bracket b;

    SketchFeature base;
    sketch::Sketch& s = base.sketch;
    const auto p1 = s.addPoint({50, 0});
    const auto p2 = s.addPoint({50, 25});
    const auto p3 = s.addPoint({0, 25});
    const auto bottom = s.addLine(sketch::kOriginId, p1);
    const auto right = s.addLine(p1, p2);
    const auto top = s.addLine(p2, p3);
    const auto left = s.addLine(p3, sketch::kOriginId);
    s.addConstraint(ConstraintKind::Horizontal, {bottom});
    s.addConstraint(ConstraintKind::Horizontal, {top});
    s.addConstraint(ConstraintKind::Vertical, {right});
    s.addConstraint(ConstraintKind::Vertical, {left});
    b.widthDimension = s.addConstraint(ConstraintKind::Distance, {bottom}, kWidth);
    s.addConstraint(ConstraintKind::Distance, {left}, kDepth);
    b.base = b.doc.add(std::move(base));

    ExtrudeFeature pad;
    pad.profiles = {b.base, {{5, 5}}};
    pad.distance = kHeight;
    pad.bodyName = b.doc.takeBodyName();
    b.doc.add(std::move(pad));

    SketchFeature hole;
    hole.plane = gp_Ax3(gp_Pnt(0, 0, kHeight), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    hole.planeName = "Face";
    sketch::Sketch& h = hole.sketch;
    const auto center = h.addPoint({14, 16});
    const auto circle = h.addCircle(center, 4.0);
    h.addConstraint(ConstraintKind::HorizontalDistance, {sketch::kOriginId, center}, 15.0);
    h.addConstraint(ConstraintKind::VerticalDistance, {sketch::kOriginId, center}, 15.0);
    b.holeDimension = h.addConstraint(ConstraintKind::Diameter, {circle}, kHoleDiameter);
    b.holeSketch = b.doc.add(std::move(hole));

    ExtrudeFeature cut;
    cut.profiles = {b.holeSketch, {{15, 15}}};
    cut.distance = -kHeight;
    cut.operation = Operation::Cut;
    b.doc.add(std::move(cut));

    FilletFeature fillet;
    fillet.sets.front().radius = kFilletRadius;
    fillet.sets.front().edges = {gp_Pnt(0, 0, 5), gp_Pnt(kWidth, 0, 5), gp_Pnt(kWidth, kDepth, 5),
                                 gp_Pnt(0, kDepth, 5)};
    b.fillet = b.doc.add(std::move(fillet));
    return b;
}

void expectAllOk(const Model& model)
{
    for (const auto& [id, status] : model.status) {
        EXPECT_EQ(status.health, FeatureHealth::Ok) << "feature " << id << ": " << status.message;
    }
}

const double kBracketVolume =
    kWidth * kDepth * kHeight - holeVolume(kHoleDiameter) - 4.0 * filletLoss(kFilletRadius, kHeight);

} // namespace

TEST(Workflow, BracketBuildsFromDimensionedSketches)
{
    const Bracket b = buildBracket();
    const Model model = recompute(b.doc);
    expectAllOk(model);
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_EQ(model.bodies.front().name, "Body1");
    EXPECT_TRUE(model.sketches.at(b.base).solve.isFullyConstrained());
    EXPECT_TRUE(model.sketches.at(b.holeSketch).solve.isFullyConstrained());
    EXPECT_NEAR(totalVolume(model), kBracketVolume, 0.5);
}

TEST(Workflow, EditingADimensionRebuildsDownstream)
{
    Bracket b = buildBracket();
    std::get<SketchFeature>(b.doc.find(b.holeSketch)->data).sketch.constraint(b.holeDimension)->value = 14.0;
    const Model model = recompute(b.doc);
    expectAllOk(model);
    const double expected = kWidth * kDepth * kHeight - holeVolume(14.0) - 4.0 * filletLoss(kFilletRadius, kHeight);
    EXPECT_NEAR(totalVolume(model), expected, 0.5);
}

TEST(Workflow, WideningTheBaseKeepsTheTimelineAlive)
{
    Bracket b = buildBracket();
    std::get<SketchFeature>(b.doc.find(b.base)->data).sketch.constraint(b.widthDimension)->value = 80.0;
    const Model model = recompute(b.doc);
    EXPECT_EQ(model.status.at(b.fillet).health, FeatureHealth::Warning);
    const double expected =
        80.0 * kDepth * kHeight - holeVolume(kHoleDiameter) - 2.0 * filletLoss(kFilletRadius, kHeight);
    EXPECT_NEAR(totalVolume(model), expected, 0.5);
}

TEST(Workflow, RollbackUndoSaveReloadAndExport)
{
    Bracket b = buildBracket();
    UndoStack undo;

    undo.push("Roll Back", b.doc);
    b.doc.setMarker(static_cast<std::size_t>(b.doc.indexOf(b.fillet)));
    EXPECT_NEAR(totalVolume(recompute(b.doc)), kWidth * kDepth * kHeight - holeVolume(kHoleDiameter), 0.5);

    undo.undo(b.doc);
    EXPECT_NEAR(totalVolume(recompute(b.doc)), kBracketVolume, 0.5);

    const auto folder = std::filesystem::temp_directory_path() / "kalve137-workflow";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const auto design = folder / "bracket.k137";
    ShapeStore shapes(shapeFolder(design));
    saveDesign(b.doc, design, shapes);
    const Model reloaded = recompute(loadDesign(design, shapes));
    expectAllOk(reloaded);
    EXPECT_NEAR(totalVolume(reloaded), kBracketVolume, 0.5);

    geometry::writeStep(reloaded.stepParts(), folder / "bracket.step");
    geometry::writeStl(reloaded.shapes(), folder / "bracket.stl");
    EXPECT_GT(std::filesystem::file_size(folder / "bracket.step"), 1000u);
    EXPECT_GT(std::filesystem::file_size(folder / "bracket.stl"), 1000u);
    std::filesystem::remove_all(folder);
}
