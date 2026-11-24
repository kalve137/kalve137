// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/sketch/badge_placement.h"
#include "app/sketch/constraint_badge.h"
#include "app/sketch/dimension_label.h"
#include "app/sketch/sketch_editor_detail.h"
#include "app/sketch/sketch_view_items.h"
#include "app/theme/fonts.h"
#include "app/theme/theme.h"

#include "render/style.h"
#include "sketch/build.h"
#include "sketch/dimension.h"
#include "sketch/draft.h"
#include "sketch/edit.h"
#include "sketch/profiles.h"
#include "sketch/snapshot.h"
#include "snapshot/mesh.h"
#include "viewer/occt_view_widget.h"
#include "viewer/style.h"

#include <AIS_Shape.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <Prs3d_DimensionAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <PrsDim_AngleDimension.hxx>
#include <PrsDim_DiameterDimension.hxx>
#include <PrsDim_LengthDimension.hxx>
#include <PrsDim_RadiusDimension.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace kalve137::app {

namespace style = viewer::style;

using namespace sketch_editor_detail;

void SketchEditor::show(const Handle(AIS_InteractiveObject)& object, std::vector<Handle(AIS_InteractiveObject)>& into,
                        bool selectable)
{
    object->SetZLayer(Graphic3d_ZLayerId_Topmost);
    m_view->aisContext()->Display(object, AIS_WireFrame, selectable ? 0 : -1, false);
    into.push_back(object);
}

void SketchEditor::showUnder(const Handle(AIS_InteractiveObject)& halo)
{
    show(halo, m_objects, false);
    m_view->aisContext()->SetDisplayPriority(halo, Graphic3d_DisplayPriority_Below);
}

void SketchEditor::clearObjects(std::vector<Handle(AIS_InteractiveObject)>& objects)
{
    for (const Handle(AIS_InteractiveObject)& object : objects) {
        m_view->aisContext()->Remove(object, false);
    }
    objects.clear();
}

void SketchEditor::redraw()
{
    clearObjects(m_objects);
    m_constraintObjects.clear();
    if (!isActive() || feature() == nullptr) {
        if (m_newView != nullptr) {
            clearView();
        }
        m_view->redraw();
        return;
    }

    sketch::Sketch shown = m_dragPreview ? *m_dragPreview : current();
    {
        sketch::Sketch probe = shown;
        m_solve = sketch::solve(probe);
    }
    std::set<Id> conflicted;
    for (Id constraint : m_solve.conflicting) {
        if (const sketch::Constraint* c = shown.constraint(constraint)) {
            conflicted.insert(c->refs.begin(), c->refs.end());
        }
    }

    if (m_newView != nullptr) {
        drawViewSketch(shown, conflicted);
    } else {
        drawSketch(shown, conflicted);
    }

    const std::optional<sketch::Constraint> pending =
        m_hasCursor ? pendingConstraint(m_cursor) : std::optional<sketch::Constraint>();
    if (pending) {
        const Id id = shown.addConstraint(pending->kind, pending->refs, pending->value);
        shown.constraint(id)->labelOffset = pending->labelOffset;
    }
    if (m_newView != nullptr) {
        drawViewConstraints(shown, m_solve);
        publish();
    } else {
        drawConstraints(shown, m_solve);
        m_view->redraw();
    }
    emit stateChanged();
}

void SketchEditor::drawSketch(const sketch::Sketch& shown, const std::set<Id>& conflicted)
{
    const gp_Ax3 ax = plane();
    if (m_display.profiles) {
        for (const TopoDS_Face& profile : sketch::findProfiles(shown, ax)) {
            Handle(AIS_Shape) object = new AIS_Shape(profile);
            object->SetColor(occt(theme::palette().viewport.plane));
            object->SetTransparency(kProfileTransparency);
            object->SetZLayer(Graphic3d_ZLayerId_Top);
            m_view->aisContext()->Display(object, AIS_Shaded, -1, false);
            m_objects.push_back(object);
        }
    }
    for (const auto& [id, curve] : shown.curves()) {
        if (curve.construction && !m_display.construction && !m_selection.contains(id)) {
            continue;
        }
        const std::optional<TopoDS_Edge> edge = sketch::makeEdge(shown, id, ax);
        if (!edge) {
            continue;
        }
        QColor color = m_solve.fullyConstrained.contains(id) ? theme::palette().viewport.sketchConstrained
                                                             : theme::palette().viewport.sketchFree;
        if (curve.construction) {
            color = theme::palette().viewport.sketchConstruction;
        }
        if (conflicted.contains(id)) {
            color = theme::palette().chrome.conflict;
        }
        if (m_selection.contains(id)) {
            color = theme::palette().chrome.accent;
        } else if (m_hover == id) {
            color = theme::palette().chrome.accent.lighter(130);
        }
        Handle(AIS_Shape) halo = new AIS_Shape(*edge);
        halo->SetColor(occt(theme::palette().viewport.halo));
        halo->SetWidth(style::kStrokeHalo);
        halo->Attributes()->SetWireAspect(
            new Prs3d_LineAspect(occt(theme::palette().viewport.halo), Aspect_TOL_SOLID, halo->Width()));
        showUnder(halo);
        Handle(AIS_Shape) object = new AIS_Shape(*edge);
        object->SetColor(occt(color));
        object->SetWidth(m_selection.contains(id) || m_hover == id ? style::kStrokeBold : style::kStrokeRegular);
        object->Attributes()->SetWireAspect(new Prs3d_LineAspect(
            occt(color), curve.construction ? Aspect_TOL_DASH : Aspect_TOL_SOLID, object->Width()));
        show(object, m_objects, false);
    }

    for (const auto& [id, point] : shown.points()) {
        if (!m_display.points && !m_selection.contains(id) && m_hover != id) {
            continue;
        }
        QColor color = m_solve.fullyConstrained.contains(id) || point.fixed
                           ? theme::palette().viewport.sketchConstrained
                           : theme::palette().viewport.sketchFree;
        if (id == sketch::kOriginId) {
            color = theme::palette().viewport.annotation;
        }
        if (m_selection.contains(id) || m_hover == id) {
            color = theme::palette().chrome.accent;
        }
        Handle(AIS_Shape) object = new AIS_Shape(BRepBuilderAPI_MakeVertex(sketch::toWorld(ax, point.pos)));
        object->Attributes()->SetPointAspect(new Prs3d_PointAspect(
            Aspect_TOM_BALL, occt(color), m_hover == id ? style::kMarkerMedium : style::kMarkerSmall));
        show(object, m_objects, false);
    }
}

void SketchEditor::drawConstraints(const sketch::Sketch& s, const sketch::SolveResult& solve)
{
    const gp_Ax3 ax = plane();
    const gp_Pln pln(ax);
    const double wpp = worldPerPixel();
    m_drawnScale = wpp;
    const double textScale = canvas()->devicePixelRatioF();
    m_editedLabel.reset();
    const std::set<Id> conflicting(solve.conflicting.begin(), solve.conflicting.end());

    const Vec2 middle = sketch::dimensionMiddle(s);
    const auto outwardFlyout = [&](Vec2 first, Vec2 second, Vec2 measured) {
        const Vec2 side{-measured.y, measured.x};
        const double sign = dot(side, (first + second) * 0.5 - middle) < 0.0 ? -1.0 : 1.0;
        return sign * wpp * kDimensionFlyoutPixels;
    };

    const auto colorFor = [&](Id id) {
        if (m_selection.contains(id)) {
            return theme::palette().chrome.accent;
        }
        return conflicting.contains(id) ? theme::palette().chrome.conflictText : theme::palette().viewport.annotation;
    };
    const std::vector<BadgePlace> badges = m_display.constraints ? placeBadges(s) : std::vector<BadgePlace>{};
    auto nextBadge = badges.begin();
    const auto showBadge = [&](const BadgePlace& place) {
        const QColor color = conflicting.contains(place.constraint) ? theme::palette().chrome.conflict
                                                                    : theme::palette().viewport.annotation;
        Handle(AIS_InteractiveObject) badge =
            makeConstraintBadge(sketch::toWorld(ax, place.center(wpp)), place.icon, color,
                                m_selection.contains(place.constraint), textScale);
        show(badge, m_objects, true);
        m_view->aisContext()->SetSelectionSensitivity(badge, 0,
                                                      static_cast<int>(kConstraintBadgePixels * textScale / 2));
        m_constraintObjects.emplace(badge.get(), place.constraint);
    };

    for (const auto& [id, constraint] : s.constraints()) {
        while (nextBadge != badges.end() && nextBadge->constraint == id) {
            showBadge(*nextBadge++);
        }
        if (!sketch::isDimension(constraint.kind) || !m_display.dimensions) {
            continue;
        }

        const auto world = [&](Vec2 p) { return sketch::toWorld(ax, p); };
        const auto build = [&](QString& text, std::optional<double>& flyout) {
            Handle(PrsDim_Dimension) dimension;
            switch (constraint.kind) {
            case ConstraintKind::Distance:
            case ConstraintKind::Offset: {
                const auto ends = sketch::measuredPoints(s, constraint);
                if (ends && sketch::distance(ends->first, ends->second) > 1e-9) {
                    const auto [a, b] = *ends;
                    dimension = new PrsDim_LengthDimension(world(a), world(b), pln);
                    flyout = outwardFlyout(a, b, b - a);
                }
                break;
            }
            case ConstraintKind::HorizontalDistance:
            case ConstraintKind::VerticalDistance: {
                const auto ends = sketch::measuredPoints(s, constraint);
                if (ends && sketch::distance(ends->first, ends->second) > 1e-9) {
                    const auto [a, b] = *ends;
                    Handle(PrsDim_LengthDimension) length = new PrsDim_LengthDimension(world(a), world(b), pln);
                    const bool horizontal = constraint.kind == ConstraintKind::HorizontalDistance;
                    length->SetDirection(horizontal ? ax.XDirection() : ax.YDirection());
                    dimension = length;
                    flyout = outwardFlyout(a, b, horizontal ? Vec2{b.x - a.x, 0.0} : Vec2{0.0, b.y - a.y});
                }
                break;
            }
            case ConstraintKind::Radius:
            case ConstraintKind::Diameter: {
                const sketch::Curve* curve = s.curve(constraint.refs[0]);
                const gp_Ax2 frame(world(s.point(curve->points[0])->pos), ax.Direction(), ax.XDirection());
                const gp_Circ circle(frame, curve->radius);
                if (constraint.kind == ConstraintKind::Radius) {
                    dimension = new PrsDim_RadiusDimension(circle, world(anchorOf(s, constraint.refs[0])));
                    text = QStringLiteral("R") + text;
                } else {
                    dimension = new PrsDim_DiameterDimension(circle, pln);
                    text = QString::fromUtf8("\u00D8") + text;
                }
                break;
            }
            case ConstraintKind::Angle: {
                const sketch::Curve* l1 = s.curve(constraint.refs[0]);
                const sketch::Curve* l2 = s.curve(constraint.refs[1]);
                const Vec2 a0 = s.point(l1->points[0])->pos;
                const Vec2 a1 = s.point(l1->points[1])->pos;
                const Vec2 b0 = s.point(l2->points[0])->pos;
                const Vec2 b1 = s.point(l2->points[1])->pos;
                const Vec2 da = a1 - a0;
                const Vec2 db = b1 - b0;
                const double denom = da.x * db.y - da.y * db.x;
                if (std::abs(denom) > 1e-12) {
                    const double t = ((b0.x - a0.x) * db.y - (b0.y - a0.y) * db.x) / denom;
                    const Vec2 corner = a0 + da * t;
                    const Vec2 far1 = sketch::distance(a0, corner) > sketch::distance(a1, corner) ? a0 : a1;
                    const Vec2 far3 = sketch::distance(b0, corner) > sketch::distance(b1, corner) ? b0 : b1;
                    const double reach = std::min(sketch::distance(far1, corner), sketch::distance(far3, corner));
                    if (reach > 1e-9) {
                        const double radius = std::min(kAngleDimensionPixels * wpp, 0.6 * reach);
                        const Vec2 p1 = corner + normalized(far1 - corner) * radius;
                        const Vec2 p3 = corner + normalized(far3 - corner) * radius;
                        dimension = new PrsDim_AngleDimension(world(p1), world(corner), world(p3));
                    }
                }
                text += QString::fromUtf8("\u00B0");
                break;
            }
            case ConstraintKind::Coincident:
            case ConstraintKind::Horizontal:
            case ConstraintKind::Vertical:
            case ConstraintKind::Parallel:
            case ConstraintKind::Perpendicular:
            case ConstraintKind::Tangent:
            case ConstraintKind::Equal:
            case ConstraintKind::PointOnCurve:
            case ConstraintKind::Midpoint:
            case ConstraintKind::Symmetric:
                break;
            }
            if (dimension.IsNull()) {
                return dimension;
            }
            Handle(Prs3d_DimensionAspect) aspect = new Prs3d_DimensionAspect();
            aspect->MakeArrows3d(false);
            aspect->MakeText3d(false);
            aspect->ArrowAspect()->SetLength(wpp * 10.0);
            aspect->TextAspect()->SetHeight(kDimensionTextPixels);
            aspect->SetTextVerticalPosition(Prs3d_DTVP_Center);
            if (flyout) {
                aspect->SetTextHorizontalPosition(Prs3d_DTHP_Center);
                aspect->SetArrowOrientation(Prs3d_DAO_Internal);
            }
            aspect->TextAspect()->SetFont(fonts::families().occtRegular.toUtf8().constData());
            dimension->SetDimensionAspect(aspect);
            dimension->SetDisplaySpecialSymbol(PrsDim_DisplaySpecialSymbol_No);
            if (const std::optional<Vec2> label = sketch::labelPosition(s, constraint)) {
                dimension->SetTextPosition(world(*label));
            } else if (flyout) {
                dimension->SetFlyout(*flyout);
            }
            dimension->SetCustomValue(TCollection_ExtendedString());
            return dimension;
        };

        QString text = formatValue(constraint.value);
        std::optional<double> flyout;
        const Handle(PrsDim_Dimension) dimension = build(text, flyout);
        if (dimension.IsNull()) {
            continue;
        }
        QString haloText = formatValue(constraint.value);
        std::optional<double> haloFlyout;
        const Handle(PrsDim_Dimension) halo = build(haloText, haloFlyout);
        halo->DimensionAspect()->SetCommonColor(occt(theme::palette().viewport.halo));
        halo->DimensionAspect()->LineAspect()->SetWidth(style::kStrokeHalo);
        showUnder(halo);

        const QColor color = colorFor(id);
        dimension->DimensionAspect()->SetCommonColor(occt(color));
        dimension->DimensionAspect()->LineAspect()->SetWidth(style::kStrokeThin);
        show(dimension, m_objects, true);
        m_constraintObjects.emplace(dimension.get(), id);

        const DimensionLabel label = makeDimensionLabel(dimension->GetTextPosition(), text, color,
                                                        m_selection.contains(id), kDimensionTextPixels, textScale);
        if (id == m_editingLabel) {
            const std::optional<sketch::DimensionLayout> layout =
                m_cameraInput ? sketch::layoutDimension(s, constraint, wpp, middle) : std::nullopt;
            if (layout) {
                m_editedLabel = labelRect(*layout);
            } else {
                const QSizeF size = QSizeF(label.pixels) / textScale;
                m_editedLabel = QRectF(
                    m_view->toScreen(dimension->GetTextPosition()) - QPointF(size.width(), size.height()) / 2.0, size);
            }
            continue;
        }
        show(label.object, m_objects, true);
        m_view->aisContext()->SetSelectionSensitivity(label.object, 0,
                                                      std::max(label.pixels.width(), label.pixels.height()) / 2);
        m_constraintObjects.emplace(label.object.get(), id);
    }
    while (nextBadge != badges.end()) {
        showBadge(*nextBadge++);
    }
}

void SketchEditor::redrawPreview()
{
    clearObjects(m_preview);
    m_viewPreview.clear();
    m_viewPreviewMarkers.clear();
    if (!isActive() || !m_hasCursor || feature() == nullptr) {
        if (m_newView != nullptr) {
            publish();
        }
        m_view->redraw();
        return;
    }
    const gp_Ax3 ax = plane();
    const sketch::Placement placement = sketch::placementOf(ax);
    const bool inView = m_newView != nullptr;
    const auto world = [&](Vec2 p) { return sketch::toWorld(ax, p); };
    const auto addEdge = [&](const TopoDS_Shape& shape) {
        Handle(AIS_Shape) object = new AIS_Shape(shape);
        object->SetColor(occt(theme::palette().chrome.accent));
        object->SetWidth(style::kStrokeThin);
        object->Attributes()->SetWireAspect(
            new Prs3d_LineAspect(occt(theme::palette().chrome.accent), Aspect_TOL_DOT, style::kStrokeThin));
        show(object, m_preview, false);
    };
    const auto addStroke = [&](snapshot::Polyline points, const QColor& color, double width, std::uint32_t pattern) {
        m_viewPreview.push_back(
            {.points = std::move(points), .color = colorOf(color), .width = width, .pattern = pattern});
    };
    const auto addPreview = [&](snapshot::Polyline points) {
        addStroke(std::move(points), theme::palette().chrome.accent, style::kStrokeThin, render::kDotPattern);
    };
    const auto addSegment = [&](Vec2 a, Vec2 b) {
        if (sketch::distance(a, b) <= 1e-9) {
            return;
        }
        if (inView) {
            addPreview({sketch::placed(placement, a), sketch::placed(placement, b)});
        } else {
            addEdge(BRepBuilderAPI_MakeEdge(world(a), world(b)));
        }
    };
    const auto addArc = [&](const gp_Circ& circle, Vec2 center, double from, double to) {
        if (inView) {
            addPreview(arcPolyline(placement, center, circle.Radius(), from, to - from));
        } else {
            addEdge(BRepBuilderAPI_MakeEdge(circle, from, to));
        }
    };
    const auto addCircle = [&](const gp_Circ& circle, Vec2 center) {
        if (inView) {
            addPreview(arcPolyline(placement, center, circle.Radius(), 0.0, kTwoPi));
        } else {
            addEdge(BRepBuilderAPI_MakeEdge(circle));
        }
    };
    const auto addMarker = [&](Vec2 p, Aspect_TypeOfMarker type, double scale) {
        if (!inView) {
            Handle(AIS_Shape) marker = new AIS_Shape(BRepBuilderAPI_MakeVertex(world(p)));
            marker->Attributes()->SetPointAspect(
                new Prs3d_PointAspect(type, occt(theme::palette().chrome.accent), scale));
            show(marker, m_preview, false);
            return;
        }
        const snapshot::Point at = sketch::placed(placement, p);
        m_viewPreviewMarkers.push_back({.anchor = {at[0], at[1], at[2]},
                                        .shape = markerShapeOf(type),
                                        .scale = markerScaleOf(scale),
                                        .color = colorOf(theme::palette().chrome.accent),
                                        .flags = render::kMarkerTopmost,
                                        .pick = 0});
    };
    const Vec2 cursor = m_cursorSnap.pos;

    switch (m_tool) {
    case Tool::Line:
        if (m_first) {
            const Vec2 end = draftLineEnd(cursor);
            addSegment(m_first->pos, end);
            if (sketch::distance(m_first->pos, end) > 1e-9) {
                const Vec2 back = normalized(protractorReference() * -1.0);
                const Vec2 dir = normalized(end - m_first->pos);
                const double sweep = std::atan2(back.x * dir.y - back.y * dir.x, dot(back, dir));
                if (std::abs(sweep) > 1e-3) {
                    const double start = angleOf(back);
                    const double radius = worldPerPixel() * kAngleArcPixels;
                    const gp_Circ circle(gp_Ax2(world(m_first->pos), ax.Direction(), ax.XDirection()), radius);
                    addArc(circle, m_first->pos, std::min(start, start + sweep), std::max(start, start + sweep));
                }
            }
        }
        break;
    case Tool::Rectangle:
        if (m_first) {
            const Vec2 a = m_first->pos;
            const Vec2 c = draftCorner(cursor);
            addSegment(a, {c.x, a.y});
            addSegment({c.x, a.y}, c);
            addSegment(c, {a.x, c.y});
            addSegment({a.x, c.y}, a);
        }
        break;
    case Tool::Circle:
        if (m_first) {
            const double r = draftRadius(cursor);
            if (r > 1e-9) {
                addCircle(gp_Circ(gp_Ax2(world(m_first->pos), ax.Direction(), ax.XDirection()), r), m_first->pos);
            }
        }
        break;
    case Tool::Arc:
        if (m_first && !m_second) {
            addSegment(m_first->pos, cursor);
        } else if (m_first && m_second && std::abs(m_arcSweep) > 1e-3) {
            const double r = sketch::distance(m_first->pos, m_second->pos);
            const double start = angleOf(m_second->pos - m_first->pos);
            const double a1 = m_arcSweep > 0.0 ? start : start + m_arcSweep;
            const double a2 = m_arcSweep > 0.0 ? start + m_arcSweep : start;
            const gp_Circ circle(gp_Ax2(world(m_first->pos), ax.Direction(), ax.XDirection()), r);
            addArc(circle, m_first->pos, a1, a2);
        }
        break;
    case Tool::CenterRectangle:
        if (m_first) {
            const Vec2 c = m_first->pos;
            const Vec2 k = draftCenterCorner(cursor);
            const Vec2 d{std::abs(k.x - c.x), std::abs(k.y - c.y)};
            const Vec2 corners[4] = {
                {c.x - d.x, c.y - d.y}, {c.x + d.x, c.y - d.y}, {c.x + d.x, c.y + d.y}, {c.x - d.x, c.y + d.y}};
            for (int i = 0; i < 4; ++i) {
                addSegment(corners[i], corners[(i + 1) % 4]);
            }
        }
        break;
    case Tool::Polygon:
        if (m_first) {
            const std::vector<Vec2> corners =
                sketch::polygonCorners(m_first->pos, draftPolygonCorner(cursor), polygonSides());
            for (std::size_t i = 0; i < corners.size(); ++i) {
                addSegment(corners[i], corners[(i + 1) % corners.size()]);
            }
        }
        break;
    case Tool::Slot:
    case Tool::CenterSlot:
    case Tool::OverallSlot:
        if (m_first) {
            const Vec2 end = m_second ? m_second->pos : draftSlotEnd(cursor);
            const double width = m_second ? draftSlotWidth(cursor) : heldPositive(1).value_or(0.0);
            const std::optional<std::pair<Vec2, Vec2>> centers = width > 1e-9 ? slotCenters(end, width) : std::nullopt;
            if (!centers) {
                addSegment(m_tool == Tool::CenterSlot ? m_first->pos * 2.0 - end : m_first->pos, end);
                break;
            }
            const auto [a, b] = *centers;
            const double r = width / 2.0;
            const Vec2 dir = normalized(b - a);
            const Vec2 across = Vec2{-dir.y, dir.x} * r;
            const double up = angleOf(across);
            addSegment(a, b);
            addSegment(a + across, b + across);
            addSegment(b - across, a - across);
            addArc(gp_Circ(gp_Ax2(world(b), ax.Direction(), ax.XDirection()), r), b, up - std::numbers::pi, up);
            addArc(gp_Circ(gp_Ax2(world(a), ax.Direction(), ax.XDirection()), r), a, up, up + std::numbers::pi);
        }
        break;
    case Tool::ArcThrough:
        if (m_first && !m_second) {
            addSegment(m_first->pos, cursor);
        } else if (m_first && m_second) {
            const Vec2 s = m_first->pos;
            const Vec2 e = m_second->pos;
            if (const std::optional<Vec2> center = sketch::circleThrough(s, e, cursor)) {
                const gp_Circ circle(gp_Ax2(world(*center), ax.Direction(), ax.XDirection()),
                                     sketch::distance(*center, s));
                const double from = angleOf(s - *center);
                const double through = wrapAngle(angleOf(cursor - *center) - from);
                const double to = wrapAngle(angleOf(e - *center) - from);
                if (through < to) {
                    addArc(circle, *center, from, from + to);
                } else {
                    addArc(circle, *center, from + to, from + kTwoPi);
                }
            } else {
                addSegment(s, e);
            }
        }
        break;
    case Tool::Point:
        break;
    case Tool::Trim: {
        const Hit hit = curveAt(cursor);
        const std::optional<sketch::Piece> piece =
            hit.kind == Hit::Kind::Curve ? sketch::trimPiece(current(), hit.id, cursor) : std::nullopt;
        if (!piece) {
            break;
        }
        if (piece->kind == CurveKind::Line && sketch::distance(piece->start, piece->end) < 1e-9) {
            break;
        }
        const bool whole = piece->whole && piece->kind == CurveKind::Circle;
        const double end = piece->startAngle + wrapAngle(piece->endAngle - piece->startAngle);
        if (inView) {
            snapshot::Polyline points =
                piece->kind == CurveKind::Line
                    ? snapshot::Polyline{sketch::placed(placement, piece->start), sketch::placed(placement, piece->end)}
                : whole
                    ? arcPolyline(placement, piece->center, piece->radius, 0.0, kTwoPi)
                    : arcPolyline(placement, piece->center, piece->radius, piece->startAngle, end - piece->startAngle);
            addStroke(std::move(points), theme::palette().chrome.conflict, style::kStrokeBold, render::kSolidPattern);
            break;
        }
        TopoDS_Shape shape;
        if (piece->kind == CurveKind::Line) {
            shape = BRepBuilderAPI_MakeEdge(world(piece->start), world(piece->end));
        } else {
            const gp_Circ circle(gp_Ax2(world(piece->center), ax.Direction(), ax.XDirection()), piece->radius);
            shape = whole ? BRepBuilderAPI_MakeEdge(circle).Shape()
                          : BRepBuilderAPI_MakeEdge(circle, piece->startAngle, end).Shape();
        }
        Handle(AIS_Shape) object = new AIS_Shape(shape);
        object->SetColor(occt(theme::palette().chrome.conflict));
        object->SetWidth(style::kStrokeBold);
        show(object, m_preview, false);
        break;
    }
    case Tool::Fillet:
        if (const std::optional<Id> corner = cornerAt(cursor)) {
            addMarker(current().point(*corner)->pos, Aspect_TOM_O_POINT, style::kMarkerMedium);
        }
        break;
    case Tool::Mirror:
        if (m_mirrorAxis != 0 && current().isCurve(m_mirrorAxis)) {
            const Hit hit = curveAt(cursor);
            sketch::Sketch probe = current();
            if (hit.kind == Hit::Kind::Curve) {
                if (const auto copies = sketch::addMirror(probe, {hit.id}, m_mirrorAxis); copies && inView) {
                    for (snapshot::SketchCurve& curve : sketch::snapshotOf(probe, placement).curves) {
                        if (std::ranges::find(*copies, curve.id) != copies->end()) {
                            addPreview(std::move(curve.points));
                        }
                    }
                } else if (copies) {
                    for (Id copy : *copies) {
                        if (const std::optional<TopoDS_Edge> edge = sketch::makeEdge(probe, copy, ax)) {
                            addEdge(*edge);
                        }
                    }
                }
            }
        }
        break;
    case Tool::Select:
    case Tool::Offset:
    case Tool::Project:
    case Tool::Dimension:
    case Tool::Coincident:
    case Tool::Horizontal:
    case Tool::Vertical:
    case Tool::Parallel:
    case Tool::Perpendicular:
    case Tool::Tangent:
    case Tool::Concentric:
    case Tool::Equal:
    case Tool::Midpoint:
    case Tool::Symmetric:
    case Tool::Fix:
        break;
    }

    const bool snapUsed = !(m_first && anyHeld());
    if (drawsGeometry(m_tool) && snapUsed) {
        for (const std::optional<Vec2>& source :
             {m_cursorSnap.alignedX, m_cursorSnap.alignedY, m_cursorSnap.extendedFrom}) {
            if (!source || sketch::distance(*source, cursor) <= 1e-9) {
                continue;
            }
            if (inView) {
                addStroke({sketch::placed(placement, *source), sketch::placed(placement, cursor)},
                          theme::palette().viewport.annotation, kInferenceLineWidth, render::kDashPattern);
            } else {
                Handle(AIS_Shape) guide = new AIS_Shape(BRepBuilderAPI_MakeEdge(world(*source), world(cursor)));
                guide->SetColor(occt(theme::palette().viewport.annotation));
                guide->SetWidth(kInferenceLineWidth);
                guide->Attributes()->SetWireAspect(new Prs3d_LineAspect(occt(theme::palette().viewport.annotation),
                                                                        Aspect_TOL_DASH, kInferenceLineWidth));
                show(guide, m_preview, false);
            }
        }
        addMarker(cursor, Aspect_TOM_PLUS, kCursorMarkerScale);
    }
    if (snapUsed && (m_cursorSnap.point != 0 || m_cursorSnap.midpointOf != 0 || m_cursorSnap.onCurve != 0)) {
        const bool midpoint = m_cursorSnap.midpointOf != 0;
        addMarker(cursor, midpoint ? Aspect_TOM_O_STAR : Aspect_TOM_O_POINT,
                  midpoint ? kMidpointMarkerScale : kSnapMarkerScale);
    }
    placeInputs(cursor);
    if (inView) {
        publish();
    } else {
        m_view->redraw();
    }
}

} // namespace kalve137::app
