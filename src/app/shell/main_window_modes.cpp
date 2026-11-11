// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/main_window.h"

#include "app/feature/feature_panel.h"
#include "app/feature/measure_panel.h"
#include "app/session/design_session.h"
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

namespace kalve137::app {

namespace {

constexpr double kEmptySceneHalfSize = 60.0;
constexpr double kPreviewGridScale = 2.0;

viewer::GridExtent previewGridExtent(const core::Model& model)
{
    Bnd_Box box;
    for (const core::Body& body : model.bodies) {
        BRepBndLib::Add(body.shape, box);
    }
    if (box.IsVoid()) {
        return {gp::Origin(), kEmptySceneHalfSize * kPreviewGridScale};
    }
    const gp_Pnt low = box.CornerMin();
    const gp_Pnt high = box.CornerMax();
    return {gp_Pnt((low.XYZ() + high.XYZ()) / 2.0), 0.5 * low.Distance(high) * kPreviewGridScale};
}

} // namespace

QString MainWindow::idleSelectionSummary() const
{
    int faces = 0;
    int edges = 0;
    double area = 0.0;
    double length = 0.0;
    for (const Pick& pick : m_scene->selected()) {
        if (pick.shape.IsNull()) {
            continue;
        }
        if (pick.kind == Pick::Kind::Face) {
            ++faces;
            area += geometry::area(pick.shape);
        } else if (pick.kind == Pick::Kind::Edge) {
            ++edges;
            length += geometry::length(pick.shape);
        }
    }
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
        for (const Pick& pick : m_scene->selected()) {
            if (const std::optional<gp_Ax3> plane =
                    pick.kind == Pick::Kind::Face ? sketchPlaneOf(pick) : std::nullopt) {
                m_scene->clearSelection();
                createSketchOn(*plane, "Face", 0);
                return;
            }
        }
    }
    m_pickingPlane = true;
    m_scene->setPickMode(PickMode::Planes);
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
    m_scene->setPickMode(PickMode::None);
    m_view->setGridExtent(std::nullopt);
    m_view->setGridLabels(false);
    m_view->setGridPlane(core::originPlane(core::OriginPlane::XY));
    updateStatus();
}

void MainWindow::leaveModes()
{
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
    m_featurePanel->cancel();
    stopPickingPlane();
    m_scene->setPickMode(PickMode::None);
    const gp_Ax3 plane = m_session->sketchPlane(id);
    m_sketchEditor->begin(id);
    refreshScene();
    m_view->setGridPlane(plane);
    m_view->setGridLabels(m_preferences.sketchGridLabels);
    if (m_preferences.lookAtSketch) {
        m_view->animateCamera([this, &plane] {
            m_view->lookAt(plane);
            fitView();
        });
    }
    updateActions();
    updateStatus();
}

void MainWindow::fitView()
{
    const bool hasSketchGeometry = std::ranges::any_of(m_session->document().features(), [](const core::Feature& f) {
        const auto* sketchData = std::get_if<core::SketchFeature>(&f.data);
        return sketchData != nullptr && !sketchData->sketch.curves().empty();
    });
    if (m_session->model().bodies.empty() && !hasSketchGeometry) {
        m_view->fitAround(kEmptySceneHalfSize);
    } else {
        m_view->fitAll();
    }
}

void MainWindow::finishSketch()
{
    m_sketchEditor->end();
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
    m_scene->clearSelection();
    updateStatus();
}

void MainWindow::onViewHover()
{
    if (!m_pickingPlane) {
        return;
    }
    const std::optional<Pick> hovered = m_scene->hovered();
    if (const std::optional<gp_Ax3> plane = hovered ? sketchPlaneOf(*hovered) : std::nullopt) {
        m_view->setGridPlane(*plane);
    }
}

void MainWindow::onViewSelection()
{
    if (!m_pickingPlane) {
        if (!m_featurePanel->isActive() && !m_sketchEditor->isActive()) {
            updateStatus();
        }
        return;
    }
    for (const Pick& pick : m_scene->takePicks()) {
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
