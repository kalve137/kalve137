// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/feature_panel.h"

#include "app/feature/feature_panel_detail.h"
#include "app/feature/value_edit.h"

#include "geometry/modeling.h"
#include "geometry/shapes.h"
#include "geometry/topology.h"
#include "log/log.h"
#include "sketch/profiles.h"

#include <BRep_Tool.hxx>
#include <TopoDS.hxx>

#include <QFormLayout>

#include <cmath>
#include <variant>

namespace kalve137::app {

using feature_panel_detail::kMaxLength;
using feature_panel_detail::makeAngleBox;
using feature_panel_detail::makeLengthBox;

namespace {

QString planeLabel(const std::string& name)
{
    if (name == "Face") {
        return FeaturePanel::tr("Face");
    }
    if (name == "XY" || name == "XZ" || name == "YZ") {
        return FeaturePanel::tr("%1 plane").arg(QString::fromStdString(name));
    }
    return QString::fromStdString(name);
}

gp_Ax3 planeThrough(const gp_Ax1& line)
{
    const gp_Vec along(line.Direction());
    for (const gp_Dir& up : {gp::DZ(), gp::DX()}) {
        gp_Vec normal(up);
        normal -= along * normal.Dot(along);
        if (normal.Magnitude() > 1e-6) {
            return gp_Ax3(line.Location(), gp_Dir(normal), line.Direction());
        }
    }
    return gp_Ax3(line.Location(), gp::DY(), line.Direction());
}

} // namespace

void FeaturePanel::pickHinge(core::PlaneFeature& plane, const Pick& pick)
{
    std::optional<gp_Ax1> hinge;
    std::optional<gp_Ax3> base;
    std::string name;
    gp_Pnt center;
    switch (pick.kind) {
    case Pick::Kind::SketchLine: {
        const auto state = m_baseModel.sketches.find(pick.feature);
        if (state == m_baseModel.sketches.end()) {
            return;
        }
        const sketch::Sketch& solved = state->second.solved;
        const sketch::Curve* line = solved.curve(static_cast<sketch::Id>(pick.index));
        if (line == nullptr || line->kind != sketch::CurveKind::Line) {
            return;
        }
        const gp_Pnt a = sketch::toWorld(state->second.plane, solved.point(line->points[0])->pos);
        const gp_Pnt b = sketch::toWorld(state->second.plane, solved.point(line->points[1])->pos);
        hinge = core::axisThrough(a, b);
        base = state->second.plane;
        name = "Sketch line";
        center = gp_Pnt((a.XYZ() + b.XYZ()) * 0.5);
        break;
    }
    case Pick::Kind::Edge: {
        if (pick.shape.IsNull() || pick.shape.ShapeType() != TopAbs_EDGE) {
            return;
        }
        const TopoDS_Edge& edge = TopoDS::Edge(pick.shape);
        hinge = geometry::lineAlong(edge);
        if (!hinge) {
            emit message(tr("Pick a straight edge"));
            return;
        }
        center = geometry::edgeMidpoint(edge);
        hinge->SetLocation(center);
        if (const core::Body* body = m_baseModel.body(pick.body())) {
            if (const auto face = geometry::flatFaceAlong(body->shape, edge)) {
                Pick facePick;
                facePick.kind = Pick::Kind::Face;
                facePick.shape = *face;
                base = sketchPlaneOf(facePick);
            }
        }
        if (!base) {
            base = planeThrough(*hinge);
        }
        name = "Edge";
        break;
    }
    case Pick::Kind::OriginAxis:
        hinge = pick.line;
        base = core::originPlane(pick.axis == core::AxisKind::Z ? core::OriginPlane::XZ : core::OriginPlane::XY);
        name = std::string(core::toString(pick.axis)) + " axis";
        center = gp::Origin();
        break;
    case Pick::Kind::ConstructionAxis: {
        const core::Feature* source = m_base.find(pick.feature);
        if (source == nullptr || pick.feature == m_editing) {
            emit message(tr("Pick an axis made before this plane"));
            return;
        }
        hinge = pick.line;
        base = planeThrough(pick.line);
        name = source->name;
        center = pick.line.Location();
        break;
    }
    case Pick::Kind::OriginPlane:
    case Pick::Kind::Face:
    case Pick::Kind::Vertex:
    case Pick::Kind::Profile:
    case Pick::Kind::SketchPoint:
    case Pick::Kind::ConstructionPlane:
    case Pick::Kind::ConstructionPoint:
        return;
    }
    if (!hinge || !base) {
        emit message(tr("The line has no length"));
        return;
    }
    plane.hinge = *hinge;
    plane.base = *base;
    plane.baseName = name;
    plane.center = center;
}

void FeaturePanel::pickMidplane(core::PlaneFeature& plane, const Pick& pick)
{
    const std::optional<PlaneSource> source = planeSourceOf(pick);
    if (!source) {
        if (pick.kind != Pick::Kind::ConstructionPlane) {
            emit message(tr("Midplanes need origin planes, construction planes or flat faces"));
        }
        return;
    }
    if (plane.baseName.empty()) {
        plane.base = source->frame;
        plane.baseName = source->name;
        plane.center = source->center;
        return;
    }
    plane.second = source->frame;
    plane.secondName = source->name;
    plane.secondCenter = source->center;
}

void FeaturePanel::pickAxisSource(core::AxisFeature& axis, const Pick& pick)
{
    const auto set = [&](core::AxisSource kind, const gp_Ax1& line, std::string name) {
        axis.kind = kind;
        axis.axis = line;
        axis.baseName = std::move(name);
        m_firstAxisPoint.reset();
    };
    const auto addPoint = [&](const gp_Pnt& point) {
        if (!m_firstAxisPoint) {
            m_firstAxisPoint = point;
            axis.baseName.clear();
            return;
        }
        if (const auto line = core::axisThrough(*m_firstAxisPoint, point)) {
            set(core::AxisSource::TwoPoints, *line, "Two points");
        } else {
            emit message(tr("Pick a second point away from the first"));
        }
    };
    switch (pick.kind) {
    case Pick::Kind::Edge: {
        if (pick.shape.IsNull() || pick.shape.ShapeType() != TopAbs_EDGE) {
            return;
        }
        const TopoDS_Edge& edge = TopoDS::Edge(pick.shape);
        if (const auto line = geometry::lineAlong(edge)) {
            set(core::AxisSource::Edge, *line, "Edge");
        } else if (const auto center = geometry::circleAxis(edge)) {
            set(core::AxisSource::Cylinder, *center, "Circle");
        } else {
            emit message(tr("Pick a straight edge or a circle"));
        }
        return;
    }
    case Pick::Kind::Face: {
        if (pick.shape.IsNull() || pick.shape.ShapeType() != TopAbs_FACE) {
            return;
        }
        const TopoDS_Face& face = TopoDS::Face(pick.shape);
        if (const auto center = geometry::revolutionAxis(face)) {
            set(core::AxisSource::Cylinder, *center, "Cylinder");
            return;
        }
        if (!sketchPlaneOf(pick)) {
            emit message(tr("Pick a cylinder, a cone or a flat face"));
            return;
        }
        const gp_Pnt at = pick.point.value_or(geometry::facePoint(face));
        if (const auto normal = geometry::faceNormal(face, at)) {
            set(core::AxisSource::FaceNormal, gp_Ax1(at, *normal), "Face normal");
        }
        return;
    }
    case Pick::Kind::Vertex:
        if (!pick.shape.IsNull() && pick.shape.ShapeType() == TopAbs_VERTEX) {
            addPoint(BRep_Tool::Pnt(TopoDS::Vertex(pick.shape)));
        }
        return;
    case Pick::Kind::ConstructionPoint:
        if (pick.point) {
            addPoint(*pick.point);
        }
        return;
    case Pick::Kind::OriginPlane:
    case Pick::Kind::Profile:
    case Pick::Kind::SketchLine:
    case Pick::Kind::SketchPoint:
    case Pick::Kind::ConstructionPlane:
    case Pick::Kind::OriginAxis:
    case Pick::Kind::ConstructionAxis:
        return;
    }
}

void FeaturePanel::pickPointSource(core::PointFeature& point, const Pick& pick)
{
    switch (pick.kind) {
    case Pick::Kind::Vertex:
        if (!pick.shape.IsNull() && pick.shape.ShapeType() == TopAbs_VERTEX) {
            point.point = BRep_Tool::Pnt(TopoDS::Vertex(pick.shape));
            point.baseName = "Vertex";
        }
        return;
    case Pick::Kind::Edge: {
        if (pick.shape.IsNull() || pick.shape.ShapeType() != TopAbs_EDGE) {
            return;
        }
        const TopoDS_Edge& edge = TopoDS::Edge(pick.shape);
        if (const auto center = geometry::circleAxis(edge)) {
            point.point = center->Location();
            point.baseName = "Circle center";
        } else {
            point.point = geometry::edgeMidpoint(edge);
            point.baseName = "Edge midpoint";
        }
        return;
    }
    case Pick::Kind::Face:
        if (!pick.shape.IsNull()) {
            point.point = geometry::areaCentroid(pick.shape);
            point.baseName = "Face center";
        }
        return;
    case Pick::Kind::OriginPlane:
    case Pick::Kind::Profile:
    case Pick::Kind::SketchLine:
    case Pick::Kind::SketchPoint:
    case Pick::Kind::ConstructionPlane:
    case Pick::Kind::OriginAxis:
    case Pick::Kind::ConstructionAxis:
    case Pick::Kind::ConstructionPoint:
        return;
    }
}

void FeaturePanel::resolvePlane(const core::PlaneFeature& plane, Resolved& resolved) const
{
    if (!plane.baseName.empty()) {
        resolved.items.push_back(plane.kind == core::PlaneKind::Angle ? QString::fromStdString(plane.baseName)
                                                                      : planeLabel(plane.baseName));
    }
    switch (plane.kind) {
    case core::PlaneKind::Offset:
    case core::PlaneKind::Angle:
        resolved.complete = !plane.baseName.empty();
        return;
    case core::PlaneKind::Midplane:
        if (!plane.secondName.empty()) {
            resolved.items.push_back(planeLabel(plane.secondName));
        }
        resolved.complete = !plane.baseName.empty() && !plane.secondName.empty();
        return;
    }
}

void FeaturePanel::buildPlaneForm(core::PlaneFeature& f, QFormLayout* fields)
{
    switch (f.kind) {
    case core::PlaneKind::Offset:
        m_size = makeLengthBox(f.offset, -kMaxLength, m_form);
        fields->addRow(tr("Distance"), m_size);
        break;
    case core::PlaneKind::Angle:
        m_size = makeAngleBox(f.angleDegrees, -360.0, 360.0, m_form);
        fields->addRow(tr("Angle"), m_size);
        break;
    case core::PlaneKind::Midplane:
        return;
    }
    connect(m_size, &ValueEdit::valueChanged, this, [this](double value) {
        auto& data = std::get<core::PlaneFeature>(*m_data);
        (data.kind == core::PlaneKind::Angle ? data.angleDegrees : data.offset) = value;
        updatePreview();
    });
    connect(m_size, &ValueEdit::accepted, this, &FeaturePanel::accept);
}

} // namespace kalve137::app
