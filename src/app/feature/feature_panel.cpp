// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/feature_panel.h"

#include "app/feature/distance_handle.h"
#include "app/feature/feature_panel_detail.h"
#include "app/feature/value_edit.h"
#include "app/session/design_session.h"
#include "app/theme/theme.h"

#include "geometry/modeling.h"
#include "geometry/shapes.h"
#include "geometry/topology.h"
#include "log/log.h"
#include "sketch/profiles.h"
#include "viewer/occt_view_widget.h"

#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>
#include <cmath>
#include <variant>

namespace kalve137::app {

QString feature_panel_detail::titleOf(const core::FeatureData& data)
{
    return std::visit(core::Overloaded{
                          [](const core::SketchFeature&) { return FeaturePanel::tr("Sketch"); },
                          [](const core::ExtrudeFeature&) { return FeaturePanel::tr("Extrude"); },
                          [](const core::RevolveFeature&) { return FeaturePanel::tr("Revolve"); },
                          [](const core::FilletFeature&) { return FeaturePanel::tr("Fillet"); },
                          [](const core::ChamferFeature&) { return FeaturePanel::tr("Chamfer"); },
                          [](const core::ShellFeature&) { return FeaturePanel::tr("Shell"); },
                          [](const core::PatternFeature&) { return FeaturePanel::tr("Pattern"); },
                          [](const core::ImportFeature&) { return FeaturePanel::tr("Import"); },
                          [](const core::PlaneFeature& f) {
                              switch (f.kind) {
                              case core::PlaneKind::Offset:
                                  return FeaturePanel::tr("Offset Plane");
                              case core::PlaneKind::Angle:
                                  return FeaturePanel::tr("Plane at Angle");
                              case core::PlaneKind::Midplane:
                                  return FeaturePanel::tr("Midplane");
                              }
                              return FeaturePanel::tr("Plane");
                          },
                          [](const core::OffsetFaceFeature&) { return FeaturePanel::tr("Press Pull"); },
                          [](const core::HoleFeature&) { return FeaturePanel::tr("Hole"); },
                          [](const core::MoveFeature&) { return FeaturePanel::tr("Move"); },
                          [](const core::AxisFeature&) { return FeaturePanel::tr("Axis"); },
                          [](const core::PointFeature&) { return FeaturePanel::tr("Point"); },
                          [](const core::AppearanceFeature&) { return FeaturePanel::tr("Appearance"); },
                      },
                      data);
}

using feature_panel_detail::titleOf;

namespace {

constexpr double kMinDistance = 0.001;
constexpr int kDragPreviewMs = 16;
constexpr double kArcHandlePixels = 90.0;

PickMode pickModeOf(const core::FeatureData& data)
{
    return std::visit(core::Overloaded{
                          [](const core::SketchFeature&) { return PickMode::None; },
                          [](const core::ExtrudeFeature&) { return PickMode::Profiles; },
                          [](const core::RevolveFeature&) { return PickMode::ProfilesAndLines; },
                          [](const core::FilletFeature&) { return PickMode::Edges; },
                          [](const core::ChamferFeature&) { return PickMode::Edges; },
                          [](const core::ShellFeature&) { return PickMode::Faces; },
                          [](const core::PatternFeature&) { return PickMode::Faces; },
                          [](const core::ImportFeature&) { return PickMode::None; },
                          [](const core::PlaneFeature& f) {
                              return f.kind == core::PlaneKind::Angle ? PickMode::Lines : PickMode::Planes;
                          },
                          [](const core::OffsetFaceFeature&) { return PickMode::Faces; },
                          [](const core::HoleFeature&) { return PickMode::HolePlacement; },
                          [](const core::MoveFeature&) { return PickMode::Faces; },
                          [](const core::AxisFeature&) { return PickMode::AxisSources; },
                          [](const core::PointFeature&) { return PickMode::PointSources; },
                          [](const core::AppearanceFeature&) { return PickMode::Faces; },
                      },
                      data);
}

bool takesPreselection(const core::FeatureData& data)
{
    return std::visit(core::Overloaded{
                          [](const core::SketchFeature&) { return false; },
                          [](const core::ExtrudeFeature&) { return false; },
                          [](const core::RevolveFeature&) { return false; },
                          [](const core::FilletFeature&) { return true; },
                          [](const core::ChamferFeature&) { return true; },
                          [](const core::ShellFeature&) { return true; },
                          [](const core::PatternFeature&) { return true; },
                          [](const core::ImportFeature&) { return false; },
                          [](const core::PlaneFeature&) { return true; },
                          [](const core::OffsetFaceFeature&) { return true; },
                          [](const core::HoleFeature&) { return false; },
                          [](const core::MoveFeature&) { return true; },
                          [](const core::AxisFeature&) { return true; },
                          [](const core::PointFeature&) { return true; },
                          [](const core::AppearanceFeature&) { return true; },
                      },
                      data);
}

bool needsBodyName(core::Operation op)
{
    switch (op) {
    case core::Operation::NewBody:
    case core::Operation::Join:
        return true;
    case core::Operation::Cut:
    case core::Operation::Intersect:
        return false;
    }
    return false;
}

void setQuietly(ValueEdit* box, double value)
{
    if (box != nullptr) {
        const QSignalBlocker blocker(box);
        box->setValue(value);
    }
}

double elapsedMs(std::chrono::steady_clock::time_point since)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

} // namespace

FeaturePanel::FeaturePanel(DesignSession& session, Scene& scene, viewer::OcctViewWidget* view, QWidget* parent)
    : QWidget(parent), m_session(session), m_scene(scene), m_view(view)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(theme::kPanelMargins);
    m_layout->addStretch();
    connect(view, &viewer::OcctViewWidget::selectionChanged, this, &FeaturePanel::onSelectionChanged);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(kDragPreviewMs);
    connect(m_previewTimer, &QTimer::timeout, this, &FeaturePanel::updatePreview);

    m_handle = new DistanceHandle(view, this);
    connect(m_handle, &DistanceHandle::dragged, this, &FeaturePanel::onHandleValue);
    connect(m_handle, &DistanceHandle::edited, this, &FeaturePanel::onHandleValue);
    connect(m_handle, &DistanceHandle::accepted, this, &FeaturePanel::accept);
    m_secondHandle = new DistanceHandle(view, this);
    m_secondHandle->setActive(false);
    connect(m_secondHandle, &DistanceHandle::dragged, this, &FeaturePanel::onSecondHandleValue);
    connect(m_secondHandle, &DistanceHandle::edited, this, &FeaturePanel::onSecondHandleValue);
    connect(m_secondHandle, &DistanceHandle::accepted, this, &FeaturePanel::accept);
    m_thirdHandle = new DistanceHandle(view, this);
    m_thirdHandle->setActive(false);
    connect(m_thirdHandle, &DistanceHandle::dragged, this, &FeaturePanel::onThirdHandleValue);
    connect(m_thirdHandle, &DistanceHandle::edited, this, &FeaturePanel::onThirdHandleValue);
    connect(m_thirdHandle, &DistanceHandle::accepted, this, &FeaturePanel::accept);
    const std::array<DistanceHandle*, 3> handles{m_handle, m_secondHandle, m_thirdHandle};
    for (DistanceHandle* handle : handles) {
        connect(handle, &DistanceHandle::activated, this, [handles, handle] {
            for (DistanceHandle* other : handles) {
                if (other != handle) {
                    other->setActive(false);
                }
            }
        });
    }
}

void FeaturePanel::setHandleIncrements(double length, double angle)
{
    for (DistanceHandle* handle : {m_handle, m_secondHandle, m_thirdHandle}) {
        handle->setIncrement(length);
    }
    m_handle->setArcIncrement(angle);
}

void FeaturePanel::schedulePreview()
{
    if (!m_previewTimer->isActive()) {
        m_previewTimer->start();
    }
}

void FeaturePanel::refreshPreview()
{
    if (isActive()) {
        schedulePreview();
    }
}

void FeaturePanel::setExtrudeDistance(double distance)
{
    auto* extrude = m_data ? std::get_if<core::ExtrudeFeature>(&*m_data) : nullptr;
    if (extrude == nullptr || m_distance == nullptr || std::abs(distance) < kMinDistance) {
        return;
    }
    setQuietly(m_distance, std::abs(distance));
    if (m_flip->isChecked() != (distance < 0.0)) {
        m_flip->setChecked(distance < 0.0);
    }
    extrude->distance = distance;
    schedulePreview();
}

void FeaturePanel::onHandleValue(double value)
{
    if (!isActive()) {
        return;
    }
    const auto setSize = [&](double& size) {
        if (value < kMinDistance) {
            return;
        }
        size = value;
        setQuietly(m_size, value);
        schedulePreview();
    };
    std::visit(core::Overloaded{
                   [](core::SketchFeature&) {},
                   [&](core::ExtrudeFeature&) { setExtrudeDistance(value); },
                   [](core::RevolveFeature&) {},
                   [&](core::FilletFeature& f) { setSize(activeSet(f).radius); },
                   [&](core::ChamferFeature& f) { setSize(f.distance); },
                   [&](core::ShellFeature& f) { setSize(f.thickness); },
                   [](core::PatternFeature&) {},
                   [](core::ImportFeature&) {},
                   [&](core::PlaneFeature& f) {
                       (f.kind == core::PlaneKind::Angle ? f.angleDegrees : f.offset) = value;
                       setQuietly(m_size, value);
                       schedulePreview();
                   },
                   [&](core::OffsetFaceFeature& f) {
                       if (std::abs(value) < kMinDistance) {
                           return;
                       }
                       f.distance = value;
                       setQuietly(m_size, value);
                       schedulePreview();
                   },
                   [&](core::HoleFeature& f) { setSize(f.depth); },
                   [&](core::MoveFeature& f) {
                       f.translation.SetX(value);
                       setQuietly(m_offsets[0], value);
                       schedulePreview();
                   },
                   [](core::AxisFeature&) {},
                   [](core::PointFeature&) {},
                   [](core::AppearanceFeature&) {},
               },
               *m_data);
}

void FeaturePanel::onThirdHandleValue(double value)
{
    if (auto* move = m_data ? std::get_if<core::MoveFeature>(&*m_data) : nullptr) {
        move->translation.SetZ(value);
        setQuietly(m_offsets[2], value);
        schedulePreview();
    }
}

void FeaturePanel::onSecondHandleValue(double value)
{
    if (auto* move = m_data ? std::get_if<core::MoveFeature>(&*m_data) : nullptr) {
        move->translation.SetY(value);
        setQuietly(m_offsets[1], value);
        schedulePreview();
        return;
    }
    auto* extrude = m_data ? std::get_if<core::ExtrudeFeature>(&*m_data) : nullptr;
    if (extrude == nullptr) {
        return;
    }
    switch (extrude->extent) {
    case core::ExtrudeExtent::OneSide:
        return;
    case core::ExtrudeExtent::Symmetric:
        setExtrudeDistance(value);
        return;
    case core::ExtrudeExtent::TwoSides:
        if (value < kMinDistance) {
            return;
        }
        extrude->secondDistance = value;
        setQuietly(m_secondDistance, value);
        schedulePreview();
        return;
    }
}

void FeaturePanel::updateHandles(const Resolved& resolved)
{
    const bool firstShow = !m_handle->isShown();
    bool shown = false;
    bool twoArrows = false;
    bool threeArrows = false;
    std::visit(
        core::Overloaded{
            [](const core::SketchFeature&) {},
            [&](const core::ExtrudeFeature& f) {
                if (f.end != core::ExtrudeEnd::Distance) {
                    return;
                }
                gp_XYZ weighted(0.0, 0.0, 0.0);
                double total = 0.0;
                gp_Dir normal;
                for (const auto& [sketchId, index] : resolved.profiles) {
                    const core::SketchState& state = m_baseModel.sketches.at(sketchId);
                    const TopoDS_Face& face = state.profiles.at(index);
                    const double a = geometry::area(face);
                    weighted += geometry::areaCentroid(face).XYZ() * a;
                    total += a;
                    normal = state.plane.Direction();
                }
                if (total <= 0.0) {
                    return;
                }
                const gp_Pnt center(weighted / total);
                m_handle->setAllowNegative(true);
                m_secondHandle->setAllowNegative(true);
                switch (f.extent) {
                case core::ExtrudeExtent::OneSide:
                    m_handle->show(center, normal, f.distance, 1.0);
                    break;
                case core::ExtrudeExtent::Symmetric:
                    m_handle->show(center, normal, f.distance, 0.5);
                    m_secondHandle->show(center, normal.Reversed(), f.distance, 0.5);
                    twoArrows = true;
                    break;
                case core::ExtrudeExtent::TwoSides:
                    m_handle->show(center, normal, f.distance, 1.0);
                    m_secondHandle->setAllowNegative(false);
                    m_secondHandle->show(center, f.distance < 0.0 ? normal : normal.Reversed(), f.secondDistance, 1.0);
                    twoArrows = true;
                    break;
                }
                shown = true;
            },
            [](const core::RevolveFeature&) {},
            [&](const core::FilletFeature& f) {
                if (resolved.edges.empty() || activeSet(f).edges.empty()) {
                    return;
                }
                const TopoDS_Edge& edge = resolved.edges.back();
                if (const auto inward = geometry::edgeBisector(resolved.lastEdgeBody, edge)) {
                    m_handle->setAllowNegative(false);
                    m_handle->show(geometry::edgeMidpoint(edge), *inward, activeSet(f).radius, 1.0);
                    shown = true;
                }
            },
            [&](const core::ChamferFeature& f) {
                if (resolved.edges.empty()) {
                    return;
                }
                const TopoDS_Edge& edge = resolved.edges.back();
                if (const auto inward = geometry::edgeBisector(resolved.lastEdgeBody, edge)) {
                    m_handle->setAllowNegative(false);
                    m_handle->show(geometry::edgeMidpoint(edge), *inward, f.distance, 1.0);
                    shown = true;
                }
            },
            [&](const core::ShellFeature& f) {
                if (resolved.faces.empty()) {
                    return;
                }
                const TopoDS_Face& face = resolved.faces.back();
                const gp_Pnt point = geometry::facePoint(face);
                if (const auto outward = geometry::faceNormal(face, point)) {
                    m_handle->setAllowNegative(false);
                    m_handle->show(point, f.inside ? outward->Reversed() : *outward, f.thickness, 1.0);
                    shown = true;
                }
            },
            [](const core::PatternFeature&) {},
            [](const core::ImportFeature&) {},
            [&](const core::PlaneFeature& f) {
                if (f.baseName.empty()) {
                    return;
                }
                m_handle->setAllowNegative(true);
                switch (f.kind) {
                case core::PlaneKind::Offset:
                    m_handle->show(f.center, f.base.Direction(), f.offset, 1.0);
                    shown = true;
                    break;
                case core::PlaneKind::Angle: {
                    const gp_Vec across = gp_Vec(f.base.Direction()).Crossed(gp_Vec(f.hinge.Direction()));
                    if (across.Magnitude() < kMinDistance) {
                        return;
                    }
                    const double radius = kArcHandlePixels * m_view->worldPerPixel();
                    m_handle->showArc(f.hinge, f.center.Translated(across.Normalized() * radius), f.angleDegrees);
                    shown = true;
                    break;
                }
                case core::PlaneKind::Midplane:
                    break;
                }
            },
            [&](const core::OffsetFaceFeature& f) {
                if (resolved.faces.empty()) {
                    return;
                }
                const TopoDS_Face& face = resolved.faces.back();
                const gp_Pnt point = geometry::facePoint(face);
                if (const auto outward = geometry::faceNormal(face, point)) {
                    m_handle->setAllowNegative(true);
                    m_handle->show(point, *outward, f.distance, 1.0);
                    shown = true;
                }
            },
            [&](const core::HoleFeature& f) {
                if (resolved.holeAxes.empty() || f.throughAll) {
                    return;
                }
                const gp_Ax1& axis = resolved.holeAxes.front();
                m_handle->setAllowNegative(false);
                m_handle->show(axis.Location(), axis.Direction(), f.depth, 1.0);
                shown = true;
            },
            [&](const core::MoveFeature& f) {
                if (f.bodies.empty()) {
                    return;
                }
                for (DistanceHandle* handle : {m_handle, m_secondHandle, m_thirdHandle}) {
                    handle->setAllowNegative(true);
                }
                const gp_Vec& t = f.translation;
                m_handle->show(f.pivot.Translated(gp_Vec(0.0, t.Y(), t.Z())), gp::DX(), t.X(), 1.0);
                m_secondHandle->show(f.pivot.Translated(gp_Vec(t.X(), 0.0, t.Z())), gp::DY(), t.Y(), 1.0);
                m_thirdHandle->show(f.pivot.Translated(gp_Vec(t.X(), t.Y(), 0.0)), gp::DZ(), t.Z(), 1.0);
                shown = true;
                twoArrows = true;
                threeArrows = true;
            },
            [](const core::AxisFeature&) {},
            [](const core::PointFeature&) {},
            [](const core::AppearanceFeature&) {},
        },
        *m_data);
    if (!shown) {
        m_handle->hide();
    }
    if (!threeArrows) {
        m_thirdHandle->hide();
    }
    if (!twoArrows) {
        m_secondHandle->hide();
        m_handle->setActive(true);
    }
    if (shown && firstShow) {
        m_handle->setActive(true);
        m_secondHandle->setActive(false);
        m_thirdHandle->setActive(false);
        m_handle->focusBox();
    }
}

void FeaturePanel::startExtrude()
{
    core::ExtrudeFeature extrude;
    m_base = m_session.document();
    m_baseModel = m_session.model();
    preselectProfile(extrude.profiles);
    extrude.operation = m_baseModel.bodies.empty() ? core::Operation::NewBody : core::Operation::Join;
    start(std::move(extrude), 0);
}

void FeaturePanel::startRevolve()
{
    core::RevolveFeature revolve;
    m_base = m_session.document();
    m_baseModel = m_session.model();
    preselectProfile(revolve.profiles);
    revolve.axis = defaultAxis(revolve.profiles);
    revolve.operation = m_baseModel.bodies.empty() ? core::Operation::NewBody : core::Operation::Join;
    start(std::move(revolve), 0);
}

void FeaturePanel::startFillet()
{
    startFresh(core::FilletFeature{});
}

void FeaturePanel::startChamfer()
{
    startFresh(core::ChamferFeature{});
}

void FeaturePanel::startShell()
{
    startFresh(core::ShellFeature{});
}

void FeaturePanel::startPattern()
{
    startFresh(core::PatternFeature{});
}

void FeaturePanel::startPressPull()
{
    m_pressPull = true;
    startFresh(core::OffsetFaceFeature{});
}

void FeaturePanel::startHole()
{
    startFresh(core::HoleFeature{});
}

void FeaturePanel::startMove()
{
    startFresh(core::MoveFeature{});
}

void FeaturePanel::startAppearance()
{
    startAppearanceOn({});
}

void FeaturePanel::startAppearanceOn(std::vector<core::BodyKey> bodies)
{
    core::AppearanceFeature appearance;
    appearance.bodies = std::move(bodies);
    m_appearanceFaces = false;
    startFresh(std::move(appearance));
}

void FeaturePanel::startOffsetPlane()
{
    startFresh(core::PlaneFeature{});
}

void FeaturePanel::startPlaneAtAngle()
{
    core::PlaneFeature plane;
    plane.kind = core::PlaneKind::Angle;
    startFresh(std::move(plane));
}

void FeaturePanel::startMidplane()
{
    core::PlaneFeature plane;
    plane.kind = core::PlaneKind::Midplane;
    startFresh(std::move(plane));
}

void FeaturePanel::startAxis()
{
    startFresh(core::AxisFeature{});
}

void FeaturePanel::startPoint()
{
    startFresh(core::PointFeature{});
}

void FeaturePanel::startFresh(core::FeatureData featureData)
{
    m_base = m_session.document();
    m_baseModel = m_session.model();
    start(std::move(featureData), 0);
    const std::vector<Pick> preselection = m_scene.takePreselection();
    if (preselection.empty() || !takesPreselection(*m_data)) {
        return;
    }
    K137_DEBUG(Input, "{} takes {} preselected picks", titleOf(*m_data).toStdString(), preselection.size());
    for (const Pick& pick : preselection) {
        togglePick(pick);
    }
    updatePreview();
}

void FeaturePanel::edit(core::FeatureId id)
{
    const core::Feature* feature = m_session.document().find(id);
    if (feature == nullptr || std::holds_alternative<core::SketchFeature>(feature->data)) {
        return;
    }
    if (std::holds_alternative<core::ImportFeature>(feature->data)) {
        emit message(tr("An imported body has no settings to edit"));
        return;
    }
    m_base = m_session.document();
    m_base.setMarker(static_cast<std::size_t>(m_base.indexOf(id)));
    m_baseModel = core::recompute(m_base);
    start(feature->data, id);
}

void FeaturePanel::start(core::FeatureData featureData, core::FeatureId editing)
{
    m_data = std::move(featureData);
    m_editing = editing;
    K137_INFO(App, "{} started{}", titleOf(*m_data).toStdString(), editing != 0 ? " as an edit" : "");
    m_firstAxisPoint.reset();
    buildForm();
    m_scene.setPickMode(m_pressPull ? PickMode::PressPull : pickModeOf(*m_data));
    updatePreview();
    emit activeChanged(true);
}

void FeaturePanel::accept()
{
    if (!isActive() || !m_ok->isEnabled()) {
        return;
    }
    core::FeatureData accepted = std::move(*m_data);
    const core::FeatureId editing = m_editing;
    const QString title = titleOf(accepted);
    K137_INFO(App, "{} accepted{}", title.toStdString(), editing != 0 ? " as an edit" : "");
    finish();

    const auto nameBody = [](core::FeatureData& d, core::Document& document) {
        std::visit(core::Overloaded{
                       [](core::SketchFeature&) {},
                       [&](core::ExtrudeFeature& f) {
                           if (f.bodyName.empty() && needsBodyName(f.operation)) {
                               f.bodyName = document.takeBodyName();
                           }
                       },
                       [&](core::RevolveFeature& f) {
                           if (f.bodyName.empty() && needsBodyName(f.operation)) {
                               f.bodyName = document.takeBodyName();
                           }
                       },
                       [](core::FilletFeature&) {},
                       [](core::ChamferFeature&) {},
                       [](core::ShellFeature&) {},
                       [](core::PatternFeature&) {},
                       [](core::ImportFeature&) {},
                       [](core::PlaneFeature&) {},
                       [](core::OffsetFaceFeature&) {},
                       [](core::HoleFeature&) {},
                       [&](core::MoveFeature& f) {
                           if (!f.copy) {
                               f.copyNames.clear();
                               return;
                           }
                           while (f.copyNames.size() < f.bodies.size()) {
                               f.copyNames.push_back(document.takeBodyName());
                           }
                           f.copyNames.resize(f.bodies.size());
                       },
                       [](core::AxisFeature&) {},
                       [](core::PointFeature&) {},
                       [](core::AppearanceFeature&) {},
                   },
                   d);
    };

    if (editing != 0) {
        const core::Feature* edited = m_session.document().find(editing);
        if (edited == nullptr) {
            emit message(tr("The feature being edited no longer exists"));
            return;
        }
        const QString name = QString::fromStdString(edited->name);
        m_session.edit(tr("Edit %1").arg(name), [&](core::Document& document) {
            nameBody(accepted, document);
            document.find(editing)->data = std::move(accepted);
        });
    } else {
        m_session.edit(title, [&](core::Document& document) {
            nameBody(accepted, document);
            document.add(std::move(accepted));
        });
    }
}

void FeaturePanel::cancel()
{
    if (isActive()) {
        K137_INFO(App, "{} canceled", titleOf(*m_data).toStdString());
        finish();
    }
}

void FeaturePanel::finish()
{
    m_data.reset();
    m_editing = 0;
    m_pressPull = false;
    m_firstAxisPoint.reset();
    if (m_form != nullptr) {
        m_form->hide();
        m_form->deleteLater();
    }
    m_form = nullptr;
    m_items = nullptr;
    m_itemsLayout = nullptr;
    m_axis = nullptr;
    m_distance = nullptr;
    m_secondDistance = nullptr;
    m_flip = nullptr;
    m_size = nullptr;
    m_offsets = {};
    m_filletSet = 0;
    m_pickingTarget = false;
    m_target = nullptr;
    m_previewTimer->stop();
    m_handle->hide();
    m_secondHandle->hide();
    m_thirdHandle->hide();
    m_scene.setHighlightedProfiles({});
    m_scene.setHighlightedEdges({});
    m_scene.setHighlightedFaces({});
    m_scene.setHighlightedBodies({});
    m_scene.setPreviewBodies({});
    m_scene.setPickMode(PickMode::None);
    emit selectionSummary({});
    emit activeChanged(false);
}

core::Document FeaturePanel::candidate() const
{
    core::Document document = m_base;
    if (m_editing != 0) {
        document.find(m_editing)->data = *m_data;
        document.setMarker(static_cast<std::size_t>(document.indexOf(m_editing)) + 1);
    } else {
        document.add(*m_data);
    }
    return document;
}

void FeaturePanel::updatePreview()
{
    if (!isActive()) {
        return;
    }
    m_previewTimer->stop();
    const auto start = std::chrono::steady_clock::now();
    const core::Document document = candidate();
    const std::size_t index = m_editing != 0 ? static_cast<std::size_t>(document.indexOf(m_editing)) : m_base.marker();
    const core::Feature& feature = document.features()[index];
    const core::Model model = core::extend(m_baseModel, feature);
    [[maybe_unused]] const double recomputeMs = elapsedMs(start);

    Resolved resolved = resolve();
    emit selectionSummary(summaryOf(resolved));
    updateHandles(resolved);
    refreshItems(resolved.items);
    m_scene.setHighlightedProfiles(std::move(resolved.profiles));
    m_scene.setHighlightedEdges(std::move(resolved.edges));
    m_scene.setHighlightedFaces(std::move(resolved.faces));
    m_scene.setHighlightedBodies(highlightedBodies(model, feature.id));
    m_scene.setPreviewBodies(previewBodies(model));
    m_scene.show(document, model, m_session);

    const core::FeatureStatus& status = model.status.at(feature.id);
    QString error;
    if (resolved.complete) {
        switch (status.health) {
        case core::FeatureHealth::Failed:
        case core::FeatureHealth::Warning:
            error = QString::fromStdString(status.message);
            break;
        case core::FeatureHealth::Ok:
        case core::FeatureHealth::Suppressed:
        case core::FeatureHealth::RolledBack:
            break;
        }
    }
    m_error->setText(error);
    m_selection->setText(selectionText());
    m_ok->setEnabled(resolved.complete && status.health != core::FeatureHealth::Failed);
    K137_TRACE(App, "Previewed {} in {:.1f} ms, {:.1f} ms of it recompute", feature.name, elapsedMs(start),
               recomputeMs);
}

void FeaturePanel::setPickingTarget(bool picking)
{
    m_pickingTarget = picking;
    m_scene.setPickMode(picking ? PickMode::Planes : pickModeOf(*m_data));
}

core::FilletSet& FeaturePanel::activeSet(core::FilletFeature& fillet)
{
    if (fillet.sets.empty()) {
        fillet.sets.emplace_back();
    }
    m_filletSet = std::min(m_filletSet, fillet.sets.size() - 1);
    return fillet.sets[m_filletSet];
}

const core::FilletSet& FeaturePanel::activeSet(const core::FilletFeature& fillet) const
{
    static const core::FilletSet kEmpty;
    if (fillet.sets.empty()) {
        return kEmpty;
    }
    return fillet.sets[std::min(m_filletSet, fillet.sets.size() - 1)];
}

std::set<core::BodyKey> FeaturePanel::previewBodies(const core::Model& model) const
{
    std::set<core::BodyKey> keys;
    if (!std::holds_alternative<core::ExtrudeFeature>(*m_data) &&
        !std::holds_alternative<core::RevolveFeature>(*m_data)) {
        return keys;
    }
    for (const core::Body& body : model.bodies) {
        const core::Body* before = m_baseModel.body(body.key());
        if (before == nullptr || !before->shape.IsSame(body.shape)) {
            keys.insert(body.key());
        }
    }
    return keys;
}

std::vector<TopoDS_Shape> FeaturePanel::highlightedBodies(const core::Model& model, core::FeatureId featureId) const
{
    std::vector<TopoDS_Shape> shapes;
    if (const auto* pattern = std::get_if<core::PatternFeature>(&*m_data)) {
        if (const core::Body* body = m_baseModel.body(pattern->body)) {
            shapes.push_back(body->shape);
        }
    } else if (const auto* move = std::get_if<core::MoveFeature>(&*m_data)) {
        if (move->copy) {
            for (const core::Body& body : model.bodies) {
                if (body.id == featureId) {
                    shapes.push_back(body.shape);
                }
            }
        } else {
            for (const core::BodyKey& key : move->bodies) {
                if (const core::Body* body = model.body(key)) {
                    shapes.push_back(body->shape);
                }
            }
        }
    }
    return shapes;
}

} // namespace kalve137::app
