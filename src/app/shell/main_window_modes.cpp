// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/main_window.h"

#include "app/feature/feature_panel.h"
#include "app/feature/measure_panel.h"
#include "app/session/design_session.h"
#include "app/view/camera_commands.h"
#include "app/view/scene.h"
#include "core/naming.h"
#include "geometry/modeling.h"
#include "geometry/shapes.h"

#include "viewer/occt_view_widget.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopoDS.hxx>

#include <QStatusBar>
#include <QStringList>

#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kalve137::app {

namespace {

constexpr double kPreviewGridScale = 2.0;

viewer::GridExtent previewGridExtent(const core::Model& model)
{
    Bnd_Box box;
    for (const core::Body& body : model.bodies) {
        BRepBndLib::Add(body.shape, box);
    }
    if (box.IsVoid()) {
        return {gp::Origin(), CameraCommands::kEmptySceneHalfSize * kPreviewGridScale};
    }
    const gp_Pnt low = box.CornerMin();
    const gp_Pnt high = box.CornerMax();
    return {gp_Pnt((low.XYZ() + high.XYZ()) / 2.0), 0.5 * low.Distance(high) * kPreviewGridScale};
}

} // namespace

QString MainWindow::idleSelectionSummary() const
{
    std::vector<TopoDS_Shape> faceShapes;
    std::vector<TopoDS_Shape> edgeShapes;
    for (const Pick& pick : m_picks->selected()) {
        if (pick.shape.IsNull()) {
            continue;
        }
        if (pick.kind == Pick::Kind::Face) {
            faceShapes.push_back(pick.shape);
        } else if (pick.kind == Pick::Kind::Edge) {
            edgeShapes.push_back(pick.shape);
        }
    }
    const auto faces = static_cast<int>(faceShapes.size());
    const auto edges = static_cast<int>(edgeShapes.size());
    const double area = geometry::totalArea(faceShapes);
    const double length = geometry::totalLength(edgeShapes);
    QStringList parts;
    if (faces > 0) {
        parts
            << tr("%1 | Area : %2 mm²").arg(faces == 1 ? tr("1 Face") : tr("%1 Faces").arg(faces)).arg(area, 0, 'f', 2);
    }
    if (edges > 0) {
        parts << tr("%1 | Length : %2 mm")
                     .arg(edges == 1 ? tr("1 Edge") : tr("%1 Edges").arg(edges))
                     .arg(length, 0, 'f', 2);
    }
    return parts.join(QStringLiteral("   "));
}

void MainWindow::startCreateSketch()
{
    m_measurePanel->stop();
    m_featurePanel->cancel();
    if (!m_sketchEditor->isActive()) {
        for (const Pick& pick : m_picks->selected()) {
            if (const std::optional<gp_Ax3> plane =
                    pick.kind == Pick::Kind::Face ? sketchPlaneOf(pick) : std::nullopt) {
                m_picks->clearSelection();
                createSketchOn(*plane, "Face", 0);
                return;
            }
        }
    }
    m_pickingPlane = true;
    updatePickMode();
    m_view->setGridExtent(previewGridExtent(m_session->model()));
    m_view->setGridLabels(m_preferences.sketchGridLabels);
    updateStatus();
}

void MainWindow::stopPickingPlane()
{
    if (!m_pickingPlane) {
        return;
    }
    m_pickingPlane = false;
    updatePickMode();
    m_view->setGridExtent(std::nullopt);
    m_view->setGridLabels(false);
    m_view->setGridPlane(core::originPlane(core::OriginPlane::XY));
    updateStatus();
}

void MainWindow::updatePickMode()
{
    if (m_measurePanel->isActive() || m_featurePanel->isActive()) {
        return;
    }
    if (m_pickingPlane) {
        m_scene->setPickMode(PickMode::Planes);
    } else if (m_sketchEditor->isActive() && m_sketchEditor->picksModel()) {
        m_scene->setPickMode(PickMode::Project);
    } else {
        m_scene->setPickMode(PickMode::None);
    }
}

void MainWindow::leaveModes()
{
    m_cameraCommands->dropSavedView();
    m_featurePanel->cancel();
    stopPickingPlane();
    finishSketch();
}

void MainWindow::createSketchOn(const gp_Ax3& plane, const std::string& planeName, core::FeatureId planeFeature,
                                std::optional<core::TopoRef> face)
{
    core::FeatureId id = 0;
    m_session->edit(tr("Create Sketch"), [&](core::Document& document) {
        core::SketchFeature sketch;
        sketch.plane = plane;
        sketch.planeName = planeName;
        sketch.planeFeature = planeFeature;
        sketch.face = std::move(face);
        id = document.add(std::move(sketch));
    });
    beginSketch(id);
}

void MainWindow::beginSketch(core::FeatureId id)
{
    const core::Feature* feature = m_session->document().find(id);
    if (feature == nullptr || !std::holds_alternative<core::SketchFeature>(feature->data)) {
        return;
    }
    const bool alreadySketching = m_sketchEditor->isActive();
    m_featurePanel->cancel();
    stopPickingPlane();
    const gp_Ax3 plane = m_session->sketchPlane(id);
    m_sketchEditor->begin(id);
    updatePickMode();
    refreshScene();
    m_view->setGridPlane(plane);
    m_view->setGridLabels(m_preferences.sketchGridLabels);
    if (m_preferences.lookAtSketch) {
        if (!alreadySketching || !m_cameraCommands->hasSavedView()) {
            m_cameraCommands->saveView();
        }
        m_cameraCommands->lookAt(plane);
    }
    updateActions();
    updateStatus();
}

bool MainWindow::sceneIsEmpty() const
{
    const bool hasSketchGeometry = std::ranges::any_of(m_session->document().features(), [](const core::Feature& f) {
        const auto* sketchData = std::get_if<core::SketchFeature>(&f.data);
        return sketchData != nullptr && !sketchData->sketch.curves().empty();
    });
    return m_session->model().bodies.empty() && !hasSketchGeometry;
}

void MainWindow::finishSketch()
{
    if (m_sketchEditor->isActive()) {
        m_cameraCommands->restoreSavedView();
    }
    m_sketchEditor->end();
    updatePickMode();
    m_view->setGridPlane(core::originPlane(core::OriginPlane::XY));
    m_view->setGridLabels(false);
    refreshScene();
    updateActions();
    updateStatus();
}

void MainWindow::editFeature(core::FeatureId id)
{
    const core::Feature* feature = m_session->document().find(id);
    if (feature == nullptr) {
        return;
    }
    if (std::holds_alternative<core::SketchFeature>(feature->data)) {
        beginSketch(id);
        return;
    }
    if (m_sketchEditor->isActive()) {
        finishSketch();
    }
    stopPickingPlane();
    m_featurePanel->cancel();
    m_featurePanel->edit(id);
}

void MainWindow::escape()
{
    if (m_measurePanel->isActive()) {
        m_measurePanel->stop();
        return;
    }
    if (m_featurePanel->isActive()) {
        m_featurePanel->cancel();
        return;
    }
    if (m_pickingPlane) {
        stopPickingPlane();
        return;
    }
    if (m_sketchEditor->isActive()) {
        m_sketchEditor->cancel();
        return;
    }
    m_picks->clearSelection();
    updateStatus();
}

void MainWindow::onViewHover()
{
    if (!m_pickingPlane) {
        return;
    }
    const std::optional<Pick> hovered = m_picks->hovered();
    if (const std::optional<gp_Ax3> plane = hovered ? sketchPlaneOf(*hovered) : std::nullopt) {
        m_view->setGridPlane(*plane);
    }
}

void MainWindow::onViewSelection()
{
    if (m_scene->pickMode() == PickMode::Project) {
        for (const Pick& pick : m_picks->takePicks()) {
            m_sketchEditor->projectPick(pick);
        }
        return;
    }
    if (!m_pickingPlane) {
        if (!m_featurePanel->isActive() && !m_sketchEditor->isActive()) {
            updateStatus();
        }
        return;
    }
    for (const Pick& pick : m_picks->takePicks()) {
        const std::optional<gp_Ax3> plane = sketchPlaneOf(pick);
        if (!plane) {
            statusBar()->showMessage(tr("Sketches need a flat face"), kMessageTimeoutMs);
            continue;
        }
        stopPickingPlane();
        std::string name = "Face";
        core::FeatureId planeFeature = 0;
        std::optional<core::TopoRef> face;
        if (pick.kind == Pick::Kind::OriginPlane) {
            name = core::toString(pick.plane);
        } else if (pick.kind == Pick::Kind::ConstructionPlane) {
            const core::Feature* source = m_session->document().find(pick.feature);
            name = source != nullptr ? source->name : "Plane";
            planeFeature = pick.feature;
        } else if (const core::Body* body = m_session->model().body(pick.body()); body != nullptr) {
            const TopoDS_Face& picked = TopoDS::Face(pick.shape);
            face = core::faceRef(*body, picked, pick.point.value_or(geometry::facePoint(picked)));
        }
        createSketchOn(*plane, name, planeFeature, std::move(face));
        return;
    }
}

} // namespace kalve137::app
