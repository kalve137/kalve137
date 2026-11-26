// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pair_shots.h"
#include "scenario.h"
#include "ui_test.h"

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/shell/startup_options.h"
#include "app/sketch/sketch_editor.h"
#include "app/theme/theme.h"
#include "camera/camera.h"
#include "core/document.h"
#include "core/naming.h"
#include "core/projection.h"
#include "core/recompute.h"
#include "geometry/modeling.h"
#include "render/rhi/viewport.h"
#include "sketch/sketch.h"
#include "viewer/occt_view_widget.h"

#include <QAction>
#include <QColor>
#include <QImage>
#include <QString>
#include <QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace kalve137::uitest {
namespace {

using ProjectTest = UiTest;

constexpr double kTolerance = 1e-4;
constexpr double kAbove = 25.0;
constexpr double kLostY = 25.0;
constexpr double kLostEnd = 20.0;
constexpr int kSettleMs = 300;
constexpr double kReach = 400.0;
constexpr double kScale = 100.0;
constexpr int kSearch = 3;
constexpr int kColorTolerance = 40;

const gp_Pnt kTopFrontEdge(0, -kBlockDepth / 2, kBlockHeight);

const core::SketchFeature* sketchOf(UiDriver& ui, core::FeatureId id)
{
    const core::Feature* feature = ui.document().find(id);
    return feature != nullptr ? std::get_if<core::SketchFeature>(&feature->data) : nullptr;
}

core::FeatureId editedSketch(UiDriver& ui)
{
    return ui.state()["sketch"].get<core::FeatureId>();
}

std::size_t projections(UiDriver& ui)
{
    const core::SketchFeature* data = sketchOf(ui, editedSketch(ui));
    return data != nullptr ? data->projections.size() : 0;
}

struct Design {
    core::Document doc;
    core::FeatureId sketch = 0;
    core::FeatureId later = 0;
};

core::FeatureId addSketchAbove(core::Document& doc)
{
    core::PlaneFeature plane;
    plane.baseName = "XY";
    plane.offset = kAbove;
    const core::FeatureId planeId = doc.add(plane);
    core::SketchFeature data;
    data.planeFeature = planeId;
    data.planeName = "Plane1";
    data.plane = core::recompute(doc).planes.at(planeId).frame;
    return doc.add(std::move(data));
}

void openSketch(UiDriver& ui, core::FeatureId sketch)
{
    ui.window().beginSketch(sketch);
    ui.settle();
}

Design blockThenSketchThenBody()
{
    Design design{blockDocument(), 0, 0};
    design.sketch = addSketchAbove(design.doc);
    core::SketchFeature base;
    sketch::Sketch& s = base.sketch;
    const auto a = s.addPoint({30, -5});
    const auto b = s.addPoint({50, -5});
    const auto c = s.addPoint({50, 5});
    const auto d = s.addPoint({30, 5});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, a);
    core::ExtrudeFeature pad;
    pad.profiles = {design.doc.add(std::move(base)), {{40.0, 0.0}}};
    pad.distance = 10.0;
    pad.bodyName = design.doc.takeBodyName();
    design.later = design.doc.add(std::move(pad));
    return design;
}

Design projectedAndLost()
{
    Design design{blockDocument(), 0, 0};
    design.sketch = addSketchAbove(design.doc);
    const core::Model model = core::recompute(design.doc);
    auto& data = std::get<core::SketchFeature>(design.doc.find(design.sketch)->data);
    const core::Body& body = model.bodies.at(0);
    const std::optional<TopoDS_Edge> edge = geometry::findEdge(body.shape, kTopFrontEdge, 1e-6);
    EXPECT_TRUE(edge.has_value());
    if (edge) {
        core::addProjectedEdge(data, *core::projectEdge(*edge, data.plane), core::edgeRef(body, *edge, kTopFrontEdge));
    }
    core::TopoRef lost;
    lost.point = gp_Pnt(0, kLostY, kBlockHeight);
    lost.names = {"f99/side/c1", "f99/end"};
    core::addProjectedEdge(data, {core::ProjectedShape::Line, {{-kLostEnd, kLostY}, {kLostEnd, kLostY}}, 0.0}, lost);
    return design;
}

TEST_F(ProjectTest, ProjectsEdgesAndFacesWithOneUndoStepEach)
{
    block(*ui);
    startSketch(*ui, "Project");
    homeView(*ui);
    const auto* editor = ui->window().findChild<app::SketchEditor*>();
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->tool(), app::SketchEditor::Tool::Project);
    EXPECT_TRUE(editor->hint().contains(QStringLiteral("project them"))) << editor->hint().toStdString();

    ui->click(kTopFrontEdge);
    ui->save("projected edge");
    ASSERT_EQ(projections(*ui), 1U) << ui->statusText().toStdString();
    EXPECT_EQ(ui->action("Undo")->text(), QStringLiteral("Undo Project"));
    const core::SketchFeature* data = sketchOf(*ui, editedSketch(*ui));
    const sketch::Curve* line = data->sketch.curve(data->projections[0].curve);
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(line->projected);
    EXPECT_TRUE(line->fixed);
    for (const sketch::Id id : line->points) {
        EXPECT_NEAR(data->sketch.point(id)->pos.y, -kBlockDepth / 2, kTolerance);
    }

    ui->click(gp_Pnt(0, 0, kBlockHeight));
    ui->save("projected face");
    EXPECT_EQ(projections(*ui), 4U) << ui->statusText().toStdString();

    ui->click(gp_Pnt(0, 0, kBlockHeight));
    EXPECT_EQ(projections(*ui), 4U);
    EXPECT_EQ(ui->statusText(), QStringLiteral("That is already projected"));

    ui->trigger("Undo Project");
    EXPECT_EQ(projections(*ui), 1U);
    ui->key(Qt::Key_Escape);
    EXPECT_EQ(editor->tool(), app::SketchEditor::Tool::Select);
}

TEST_F(ProjectTest, IgnoresLaterBodiesAndRefusesSlantedFacesWithAReason)
{
    Design design = blockThenSketchThenBody();
    ui->session().reset(std::move(design.doc), {});
    homeView(*ui);
    openSketch(*ui, design.sketch);
    ui->trigger("Project");
    homeView(*ui);

    ui->click(gp_Pnt(40, 0, 10));
    EXPECT_TRUE(ui->statusText().contains(QStringLiteral("project them"))) << ui->statusText().toStdString();
    EXPECT_EQ(projections(*ui), 0U);

    ui->click(gp_Pnt(0, -kBlockDepth / 2, kBlockHeight / 2));
    EXPECT_EQ(ui->statusText(), QStringLiteral("Only flat faces parallel to the sketch project"));
    EXPECT_EQ(projections(*ui), 0U);

    ui->click(kTopFrontEdge);
    ui->save("projected before a later body");
    ASSERT_EQ(projections(*ui), 1U) << ui->statusText().toStdString();
    const core::SketchFeature* data = sketchOf(*ui, design.sketch);
    for (const sketch::Id id : data->sketch.curve(data->projections[0].curve)->points) {
        EXPECT_NEAR(data->sketch.point(id)->pos.y, -kBlockDepth / 2, kTolerance);
    }
}

TEST_F(ProjectTest, BreakLinkKeepsTheCurveAndDeleteDropsTheLink)
{
    Design design = projectedAndLost();
    ui->session().reset(std::move(design.doc), {});
    homeView(*ui);
    openSketch(*ui, design.sketch);
    EXPECT_EQ(ui->state()["sketch"].get<core::FeatureId>(), design.sketch);
    const core::Model& model = ui->session().model();
    ASSERT_EQ(model.sketches.at(design.sketch).lostProjections.size(), 1U);
    EXPECT_EQ(model.status.at(design.sketch).health, core::FeatureHealth::Warning);

    const core::SketchFeature* data = sketchOf(*ui, design.sketch);
    const sketch::Id kept = data->projections[0].curve;
    const sketch::Id lost = data->projections[1].curve;
    auto* editor = ui->window().findChild<app::SketchEditor*>();
    ASSERT_NE(editor, nullptr);
    ui->click(gp_Pnt(0, kLostY, kAbove));
    ASSERT_EQ(editor->hitAt(ui->at(gp_Pnt(0, kLostY, kAbove))).id, lost);
    ui->trigger("Break Link");
    data = sketchOf(*ui, design.sketch);
    ASSERT_EQ(data->projections.size(), 1U);
    ASSERT_NE(data->sketch.curve(lost), nullptr);
    EXPECT_FALSE(data->sketch.curve(lost)->projected);
    EXPECT_TRUE(data->sketch.curve(lost)->fixed);
    EXPECT_EQ(ui->session().model().status.at(design.sketch).health, core::FeatureHealth::Ok);

    ui->click(gp_Pnt(0, -kBlockDepth / 2, kAbove));
    ui->trigger("Delete");
    data = sketchOf(*ui, design.sketch);
    EXPECT_TRUE(data->projections.empty());
    EXPECT_EQ(data->sketch.curve(kept), nullptr);
}

TEST_F(ProjectTest, PickAgainMovesALostLinkToANewEdgeWithOneUndoStep)
{
    Design design = projectedAndLost();
    ui->session().reset(std::move(design.doc), {});
    homeView(*ui);
    openSketch(*ui, design.sketch);
    const sketch::Id lost = sketchOf(*ui, design.sketch)->projections[1].curve;
    auto* editor = ui->window().findChild<app::SketchEditor*>();
    ASSERT_NE(editor, nullptr);

    ui->click(gp_Pnt(0, kLostY, kAbove));
    ASSERT_EQ(editor->hitAt(ui->at(gp_Pnt(0, kLostY, kAbove))).id, lost);
    ui->trigger("Pick Again");
    EXPECT_EQ(editor->tool(), app::SketchEditor::Tool::Project);
    EXPECT_TRUE(editor->hint().contains(QStringLiteral("should follow"))) << editor->hint().toStdString();

    homeView(*ui);
    ui->click(kTopFrontEdge);
    ui->save("picked again");
    ASSERT_EQ(editor->tool(), app::SketchEditor::Tool::Select) << ui->statusText().toStdString();
    EXPECT_EQ(ui->action("Undo")->text(), QStringLiteral("Undo Pick Again"));
    const core::SketchFeature* data = sketchOf(*ui, design.sketch);
    ASSERT_EQ(data->projections.size(), 2U);
    EXPECT_EQ(data->projections[1].curve, lost);
    for (const sketch::Id id : data->sketch.curve(lost)->points) {
        EXPECT_NEAR(data->sketch.point(id)->pos.y, -kBlockDepth / 2, kTolerance);
    }
    EXPECT_TRUE(ui->session().model().sketches.at(design.sketch).lostProjections.empty());
    EXPECT_EQ(ui->session().model().status.at(design.sketch).health, core::FeatureHealth::Ok);

    ui->trigger("Undo Pick Again");
    EXPECT_EQ(ui->session().model().sketches.at(design.sketch).lostProjections.size(), 1U);
}

TEST_F(ProjectTest, ProjectsAVertexAsAFixedPoint)
{
    block(*ui);
    startSketch(*ui, "Project");
    homeView(*ui);
    const gp_Pnt corner(kBlockWidth / 2, -kBlockDepth / 2, kBlockHeight);
    ui->click(corner);
    ui->save("projected vertex");
    ASSERT_EQ(projections(*ui), 1U) << ui->statusText().toStdString();
    const core::SketchFeature* data = sketchOf(*ui, editedSketch(*ui));
    const core::Projection& link = data->projections[0];
    EXPECT_EQ(link.kind, core::ProjectionKind::Vertex);
    EXPECT_EQ(link.curve, 0U);
    const sketch::Point* point = data->sketch.point(link.point);
    ASSERT_NE(point, nullptr);
    EXPECT_TRUE(point->projected);
    EXPECT_TRUE(point->fixed);
    EXPECT_NEAR(point->pos.x, kBlockWidth / 2, kTolerance);
    EXPECT_NEAR(point->pos.y, -kBlockDepth / 2, kTolerance);
}

TEST_F(ProjectTest, KeepsProjectingAfterAnUndoFromOutsideTheTool)
{
    block(*ui);
    startSketch(*ui, "Project");
    homeView(*ui);
    const auto* editor = ui->window().findChild<app::SketchEditor*>();
    ASSERT_NE(editor, nullptr);
    const std::size_t curves = editor->shownSketch().curves().size();
    ui->click(kTopFrontEdge);
    ASSERT_EQ(projections(*ui), 1U) << ui->statusText().toStdString();

    ui->session().undo();
    ui->settle();
    EXPECT_EQ(projections(*ui), 0U);
    EXPECT_EQ(editor->tool(), app::SketchEditor::Tool::Project);
    EXPECT_EQ(editor->shownSketch().curves().size(), curves);

    ui->click(kTopFrontEdge);
    EXPECT_EQ(projections(*ui), 1U) << ui->statusText().toStdString();
    EXPECT_EQ(ui->action("Undo")->text(), QStringLiteral("Undo Project"));
}

TEST_F(ProjectTest, FixRefusesProjectedCurves)
{
    Design design = projectedAndLost();
    ui->session().reset(std::move(design.doc), {});
    homeView(*ui);
    openSketch(*ui, design.sketch);
    const sketch::Id kept = sketchOf(*ui, design.sketch)->projections[0].curve;

    ui->trigger("Fix");
    ui->click(gp_Pnt(0, -kBlockDepth / 2, kAbove));
    EXPECT_EQ(ui->statusText(), QStringLiteral("Projected curves follow the model. Break the link first"));
    const sketch::Curve* curve = sketchOf(*ui, design.sketch)->sketch.curve(kept);
    ASSERT_NE(curve, nullptr);
    EXPECT_TRUE(curve->fixed);
    EXPECT_TRUE(curve->projected);
}

TEST_F(ProjectTest, OpensLinkedSketchesOnTheModelSolve)
{
    Design design = projectedAndLost();
    auto& data = std::get<core::SketchFeature>(design.doc.find(design.sketch)->data);
    const sketch::Id anchor = data.sketch.curve(data.projections[0].curve)->points[0];
    const sketch::Id free = data.sketch.addPoint({5.0, 5.0});
    data.sketch.addConstraint(sketch::ConstraintKind::Coincident, {free, anchor});
    ui->session().reset(std::move(design.doc), {});
    homeView(*ui);
    openSketch(*ui, design.sketch);

    const sketch::Sketch& solved = ui->session().model().sketches.at(design.sketch).solved;
    const auto* editor = ui->window().findChild<app::SketchEditor*>();
    ASSERT_NE(editor, nullptr);
    const sketch::Point* shown = editor->shownSketch().point(free);
    ASSERT_NE(shown, nullptr);
    EXPECT_NEAR(shown->pos.x, solved.point(free)->pos.x, kTolerance);
    EXPECT_NEAR(shown->pos.y, solved.point(free)->pos.y, kTolerance);
    EXPECT_NEAR(shown->pos.y, -kBlockDepth / 2, kTolerance);
}

void lookDown(app::MainWindow& window)
{
    render::Viewport& canvas = *window.viewport();
    camera::Camera camera = canvas.camera();
    camera.setProjectionType(camera::Projection::Orthographic);
    EXPECT_TRUE(camera.setEyeAndCenter({0.0, 0.0, kReach}, {0.0, 0.0, 0.0}));
    EXPECT_TRUE(camera.setUp({0.0, 1.0, 0.0}));
    EXPECT_TRUE(camera.setScale(kScale));
    canvas.setCamera(camera);
}

int nearestGap(const QImage& image, QPointF at, double ratio, const QColor& color)
{
    const int cx = static_cast<int>(std::lround(at.x() * ratio));
    const int cy = static_cast<int>(std::lround(at.y() * ratio));
    int best = 1 << 20;
    for (int y = cy - kSearch; y <= cy + kSearch; ++y) {
        for (int x = cx - kSearch; x <= cx + kSearch; ++x) {
            if (image.rect().contains(x, y)) {
                best = std::min(best, gapOf(image.pixel(x, y), color.rgb()));
            }
        }
    }
    return best;
}

class ProjectPairTest : public testing::Test {
protected:
    void SetUp() override
    {
        resetSettings();
        takeProblems();
    }

    void TearDown() override
    {
        for (const std::string& problem : takeProblems()) {
            ADD_FAILURE() << problem;
        }
        resetSettings();
    }

    bool start()
    {
        if (app::startupFromEnvironment().view != app::ViewKind::New) {
            return false;
        }
        m_new = showPairWindow(app::ViewKind::New);
        if (m_new->viewport() == nullptr || !m_new->viewport()->isReady()) {
            return false;
        }
        m_old = showPairWindow(app::ViewKind::Old);
        EXPECT_TRUE(m_old->oldView()->isReady());
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            if (QAction* grid = actionNamed(*window, QStringLiteral("Show Grid"));
                grid != nullptr && grid->isChecked()) {
                grid->trigger();
            }
            Design design = projectedAndLost();
            auto* session = window->findChild<app::DesignSession*>();
            session->reset(std::move(design.doc), {});
            session->setOriginVisible(false);
            window->beginSketch(design.sketch);
        }
        QTest::qWait(kSettleMs);
        m_folder = pairFolder("ProjectPairTest");
        std::filesystem::create_directories(m_folder);
        return true;
    }

    std::unique_ptr<app::MainWindow> m_old;
    std::unique_ptr<app::MainWindow> m_new;
    std::filesystem::path m_folder;
};

TEST_F(ProjectPairTest, ProjectedCurvesDrawInTheirColorAndLostOnesInTheConflictColor)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    const Shots shots = grabPair(*m_old, *m_new, lookDown);
    savePair(shots, m_folder, "projected and lost");
    const double ratio = shots.viewport.pixelRatio;
    const QPointF kept = screenOf(shots.camera, shots.viewport, {0.0, -kBlockDepth / 2, kAbove});
    const QPointF lost = screenOf(shots.camera, shots.viewport, {0.0, kLostY, kAbove});
    const QPointF lostEnd = screenOf(shots.camera, shots.viewport, {kLostEnd, kLostY, kAbove});
    const app::theme::Palette& colors = app::theme::palette();
    EXPECT_LE(nearestGap(shots.fresh, kept, ratio, colors.viewport.sketchProjected), kColorTolerance);
    EXPECT_LE(nearestGap(shots.fresh, lost, ratio, colors.chrome.conflict), kColorTolerance);
    EXPECT_GT(nearestGap(shots.fresh, kept, ratio, colors.chrome.conflict), kColorTolerance);
    EXPECT_GT(nearestGap(shots.fresh, lost, ratio, colors.viewport.sketchProjected), kColorTolerance);
    EXPECT_GT(nearestGap(shots.fresh, lostEnd, ratio, colors.viewport.sketchProjected), kColorTolerance);
}

} // namespace
} // namespace kalve137::uitest
