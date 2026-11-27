// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-FileCopyrightText: 1999-2014 OPEN CASCADE SAS
// SPDX-License-Identifier: GPL-3.0-or-later AND LGPL-2.1-only WITH OCCT-exception-1.0

#include "viewer/camera_sync.h"
#include "viewer/occt_view_widget.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_BndBox3d.hxx>
#include <Graphic3d_CStructure.hxx>
#include <Graphic3d_CView.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Structure.hxx>
#include <NCollection_List.hxx>
#include <NCollection_Map.hxx>
#include <NCollection_Vec2.hxx>
#include <NCollection_Vec3.hxx>
#include <PrsMgr_Presentation.hxx>
#include <gp.hxx>
#include <gp_Vec.hxx>
#include <gp_XYZ.hxx>

#include <array>
#include <cmath>

namespace kalve137::viewer {

namespace {

using Structures = NCollection_Map<Handle(Graphic3d_Structure)>;

Structures displayedStructures(const Handle(AIS_InteractiveContext)& context, const Handle(V3d_View)& view,
                               bool hasWindow)
{
    Structures structures;
    if (hasWindow) {
        view->View()->DisplayedStructures(structures);
        return structures;
    }
    NCollection_List<Handle(AIS_InteractiveObject)> objects;
    context->DisplayedObjects(objects);
    for (const Handle(AIS_InteractiveObject)& object : objects) {
        for (const Handle(PrsMgr_Presentation)& presentation : object->Presentations()) {
            if (presentation->IsDisplayed()) {
                structures.Add(presentation);
            }
        }
    }
    return structures;
}

std::array<gp_Pnt, 8> corners(const gp_XYZ& low, const gp_XYZ& high)
{
    return {gp_Pnt(low.X(), low.Y(), low.Z()),   gp_Pnt(low.X(), low.Y(), high.Z()),
            gp_Pnt(low.X(), high.Y(), low.Z()),  gp_Pnt(low.X(), high.Y(), high.Z()),
            gp_Pnt(high.X(), low.Y(), low.Z()),  gp_Pnt(high.X(), low.Y(), high.Z()),
            gp_Pnt(high.X(), high.Y(), low.Z()), gp_Pnt(high.X(), high.Y(), high.Z())};
}

} // namespace

void OcctViewWidget::setSyncedCamera(const camera::Camera& camera, const camera::Viewport& viewport)
{
    if (!viewport.isValid()) {
        return;
    }
    m_syncedViewport = viewport;
    applyCamera(camera, *m_view->Camera());
    m_view->Invalidate();
}

std::optional<camera::Viewport> OcctViewWidget::viewport() const
{
    if (!isReady()) {
        return m_syncedViewport;
    }
    int width = 0;
    int height = 0;
    m_view->Window()->Size(width, height);
    const camera::Viewport viewport{width, height, m_view->Window()->DevicePixelRatio()};
    return viewport.isValid() ? std::optional<camera::Viewport>(viewport) : std::nullopt;
}

camera::Camera OcctViewWidget::currentCamera() const
{
    return readCamera(m_view->Camera());
}

std::optional<gp_Pnt> OcctViewWidget::pickPoint(const camera::Pixel& pixel, bool onRay)
{
    if (!isReady()) {
        return std::nullopt;
    }
    gp_Pnt point;
    if (!PickPoint(point, m_context, m_view, NCollection_Vec2<int>(pixel.x, pixel.y), onRay)) {
        return std::nullopt;
    }
    return point;
}

gp_Pnt OcctViewWidget::gravityPoint() const
{
    if (isReady()) {
        return m_context->GravityPoint(m_view);
    }
    const Structures structures = displayedStructures(m_context, m_view, false);
    bool hasSelection = false;
    for (Structures::Iterator it(structures); it.More() && !hasSelection; it.Next()) {
        hasSelection = it.Key()->IsHighlighted() && it.Key()->IsVisible();
    }
    gp_XYZ sum(0.0, 0.0, 0.0);
    int count = 0;
    for (Structures::Iterator it(structures); it.More(); it.Next()) {
        const Handle(Graphic3d_Structure)& structure = it.Key();
        if (!structure->IsVisible() || structure->IsInfinite() || (hasSelection && !structure->IsHighlighted()) ||
            !structure->TransformPersistence().IsNull()) {
            continue;
        }
        const Graphic3d_BndBox3d& box = structure->CStructure()->BoundingBox();
        if (!box.IsValid()) {
            continue;
        }
        const NCollection_Vec3<double>& low = box.CornerMin();
        const NCollection_Vec3<double>& high = box.CornerMax();
        for (const gp_Pnt& corner : corners(gp_XYZ(low.x(), low.y(), low.z()), gp_XYZ(high.x(), high.y(), high.z()))) {
            const gp_Pnt projected = m_view->Camera()->Project(corner);
            if (std::abs(projected.X()) <= 1.0 && std::abs(projected.Y()) <= 1.0) {
                sum += corner.XYZ();
                ++count;
            }
        }
    }
    if (count == 0) {
        const Bnd_Box box = m_view->View()->MinMaxValues(structures, false);
        if (!box.IsVoid()) {
            for (const gp_Pnt& corner : corners(box.CornerMin().XYZ(), box.CornerMax().XYZ())) {
                sum += corner.XYZ();
                ++count;
            }
        }
    }
    return count > 0 ? gp_Pnt(sum / count) : gp::Origin();
}

std::optional<camera::Box> OcctViewWidget::sceneBounds() const
{
    const Structures structures = displayedStructures(m_context, m_view, isReady());
    const Bnd_Box box = m_view->View()->MinMaxValues(structures, false);
    if (box.IsVoid()) {
        return std::nullopt;
    }
    const gp_Pnt min = box.CornerMin();
    const gp_Pnt max = box.CornerMax();
    return camera::Box{{min.X(), min.Y(), min.Z()}, {max.X(), max.Y(), max.Z()}};
}

std::optional<gp_Pnt> OcctViewWidget::pickOnPlaneWithoutWindow(const QPointF& pos, const gp_Ax3& plane) const
{
    if (!m_syncedViewport) {
        return std::nullopt;
    }
    const camera::Viewport& viewport = *m_syncedViewport;
    const camera::Pixel pixel = camera::toPixel({pos.x(), pos.y()}, viewport);
    const double x = 2.0 * pixel.x / viewport.width - 1.0;
    const double y = 2.0 * (viewport.height - 1 - pixel.y) / viewport.height - 1.0;
    const gp_Pnt origin = m_view->Camera()->UnProject(gp_Pnt(x, y, -1.0));
    const gp_Pnt behind = m_view->Camera()->UnProject(gp_Pnt(x, y, -11.0));
    const gp_Vec direction = gp_Vec(behind, origin).Normalized();
    const gp_Vec normal(plane.Direction());
    const double along = direction.Dot(normal);
    if (std::abs(along) < 1e-9) {
        return std::nullopt;
    }
    const double t = gp_Vec(origin, plane.Location()).Dot(normal) / along;
    return origin.Translated(direction * t);
}

double OcctViewWidget::worldPerPixelWithoutWindow() const
{
    if (!m_syncedViewport) {
        return 1.0;
    }
    return m_view->Camera()->ViewDimensions().X() / m_syncedViewport->width * m_syncedViewport->pixelRatio;
}

QPointF OcctViewWidget::toScreenWithoutWindow(const gp_Pnt& point) const
{
    if (!m_syncedViewport) {
        return {};
    }
    const gp_Pnt projected = m_view->Camera()->Project(point);
    const camera::Viewport& viewport = *m_syncedViewport;
    const double x = (projected.X() + 1.0) * 0.5 * viewport.width;
    const double y = viewport.height - 1 - (projected.Y() + 1.0) * 0.5 * viewport.height;
    return {x / viewport.pixelRatio, y / viewport.pixelRatio};
}

} // namespace kalve137::viewer
