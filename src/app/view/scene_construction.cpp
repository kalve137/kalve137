// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/scene.h"

#include "app/theme/theme.h"
#include "app/view/scene_detail.h"

#include "viewer/occt_view_widget.h"
#include "viewer/style.h"

#include <AIS_Point.hxx>
#include <AIS_Shape.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>
#include <gp_Pln.hxx>

#include <utility>

namespace kalve137::app {

namespace style = viewer::style;

using scene_detail::hoverInOwnLayer;
using scene_detail::kNotSelectable;
using scene_detail::kOriginPlaneTransparency;
using scene_detail::kWholeObject;
using scene_detail::makeSurface;
using scene_detail::makeWire;
using scene_detail::occt;

namespace {

constexpr double kOriginPlanePixels = 80.0;
constexpr double kOriginAxisToPlane = 1.3;
constexpr double kOriginPointScale = style::kMarkerMedium;
constexpr double kConstructionPlanePixels = 120.0;
constexpr double kConstructionAxisPixels = 160.0;
constexpr double kConstructionPointScale = style::kMarkerMedium;
constexpr double kDatumAxisWidth = style::kStrokeRegular;

void pinTo(const Handle(AIS_InteractiveObject)& object, const gp_Pnt& anchor)
{
    object->SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_ZoomPers, anchor));
}

bool axesPickable(PickMode mode)
{
    return mode == PickMode::Lines || mode == PickMode::ProfilesAndLines || mode == PickMode::Faces;
}

} // namespace

void Scene::showOrigin()
{
    if (!m_originVisible && m_mode != PickMode::Planes && m_mode != PickMode::Lines) {
        return;
    }
    const Handle(AIS_InteractiveContext)& context = m_view->aisContext();
    const double size = kOriginPlanePixels * m_view->devicePixelRatioF();
    for (core::OriginPlane plane : {core::OriginPlane::XY, core::OriginPlane::XZ, core::OriginPlane::YZ}) {
        const TopoDS_Face face = BRepBuilderAPI_MakeFace(gp_Pln(core::originPlane(plane)), 0.0, size, 0.0, size);
        Handle(AIS_Shape) object = makeSurface(face, theme::kOriginPlane, kOriginPlaneTransparency);
        pinTo(object, gp::Origin());
        object->SetZLayer(Graphic3d_ZLayerId_Topmost);
        hoverInOwnLayer(object, context);
        Pick pick;
        pick.kind = Pick::Kind::OriginPlane;
        pick.plane = plane;
        display(object, m_mode == PickMode::Planes ? kWholeObject : kNotSelectable, pick);
    }
    const std::pair<core::AxisKind, QColor> axes[] = {
        {core::AxisKind::X, theme::kAxisX}, {core::AxisKind::Y, theme::kAxisY}, {core::AxisKind::Z, theme::kAxisZ}};
    for (const auto& [kind, color] : axes) {
        const gp_Dir direction = kind == core::AxisKind::X ? gp::DX() : kind == core::AxisKind::Y ? gp::DY() : gp::DZ();
        const gp_Pnt end = gp::Origin().Translated(gp_Vec(direction) * size * kOriginAxisToPlane);
        Handle(AIS_Shape) axis =
            makeWire(BRepBuilderAPI_MakeEdge(gp::Origin(), end), color, kDatumAxisWidth, Aspect_TOL_DOTDASH);
        pinTo(axis, gp::Origin());
        axis->SetZLayer(Graphic3d_ZLayerId_Topmost);
        Pick pick;
        pick.kind = Pick::Kind::OriginAxis;
        pick.axis = kind;
        pick.line = gp_Ax1(gp::Origin(), direction);
        display(axis, axesPickable(m_mode) ? kWholeObject : kNotSelectable, pick);
    }
    Handle(AIS_Point) point = new AIS_Point(new Geom_CartesianPoint(gp::Origin()));
    point->SetMarker(Aspect_TOM_BALL);
    point->SetColor(occt(theme::kOriginPoint));
    point->Attributes()->PointAspect()->SetScale(kOriginPointScale);
    point->SetZLayer(Graphic3d_ZLayerId_Topmost);
    display(point, kNotSelectable, std::nullopt, AIS_WireFrame);
}

void Scene::showConstructionPlanes()
{
    const double half = 0.5 * kConstructionPlanePixels * m_view->devicePixelRatioF();
    for (const auto& [id, plane] : m_model.planes) {
        if (m_hidden.contains(id)) {
            continue;
        }
        const gp_Ax3 local(gp::Origin(), plane.frame.Direction(), plane.frame.XDirection());
        const TopoDS_Face face = BRepBuilderAPI_MakeFace(gp_Pln(local), -half, half, -half, half);
        Handle(AIS_Shape) object = makeSurface(face, theme::kOriginPlane, kOriginPlaneTransparency);
        pinTo(object, plane.center);
        hoverInOwnLayer(object, m_view->aisContext());
        Pick pick;
        pick.kind = Pick::Kind::ConstructionPlane;
        pick.feature = id;
        pick.construction = plane;
        display(object, m_mode == PickMode::Planes ? kWholeObject : kNotSelectable, pick);
    }
}

void Scene::showConstructionAxes()
{
    const double half = 0.5 * kConstructionAxisPixels * m_view->devicePixelRatioF();
    const bool pickable = axesPickable(m_mode);
    for (const auto& [id, axis] : m_model.axes) {
        if (m_hidden.contains(id)) {
            continue;
        }
        const gp_Vec along = gp_Vec(axis.Direction()) * half;
        const gp_Pnt from = gp::Origin().Translated(-along);
        const gp_Pnt to = gp::Origin().Translated(along);
        Handle(AIS_Shape) object =
            makeWire(BRepBuilderAPI_MakeEdge(from, to), theme::kOriginPlane, kDatumAxisWidth, Aspect_TOL_DASH);
        pinTo(object, axis.Location());
        Pick pick;
        pick.kind = Pick::Kind::ConstructionAxis;
        pick.feature = id;
        pick.line = axis;
        display(object, pickable ? kWholeObject : kNotSelectable, pick);
    }
}

void Scene::showConstructionPoints()
{
    for (const auto& [id, location] : m_model.points) {
        if (m_hidden.contains(id)) {
            continue;
        }
        Handle(AIS_Point) point = new AIS_Point(new Geom_CartesianPoint(location));
        point->SetMarker(Aspect_TOM_O_PLUS);
        point->SetColor(occt(theme::kOriginPlane));
        point->Attributes()->PointAspect()->SetScale(kConstructionPointScale);
        point->SetZLayer(Graphic3d_ZLayerId_Topmost);
        Pick pick;
        pick.kind = Pick::Kind::ConstructionPoint;
        pick.feature = id;
        pick.point = location;
        display(point, m_mode == PickMode::AxisSources ? kWholeObject : kNotSelectable, pick, AIS_WireFrame);
    }
}

} // namespace kalve137::app
