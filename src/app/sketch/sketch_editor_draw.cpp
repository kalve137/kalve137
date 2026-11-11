// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/sketch/constraint_badge.h"
#include "app/sketch/dimension_label.h"
#include "app/sketch/sketch_editor_detail.h"
#include "app/theme/fonts.h"
#include "app/theme/theme.h"

#include "sketch/build.h"
#include "sketch/dimension.h"
#include "sketch/draft.h"
#include "sketch/edit.h"
#include "sketch/profiles.h"
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
#include <limits>
#include <optional>
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

    const gp_Ax3 ax = plane();
    if (m_display.profiles) {
        for (const TopoDS_Face& profile : sketch::findProfiles(shown, ax)) {
            Handle(AIS_Shape) object = new AIS_Shape(profile);
            object->SetColor(occt(theme::kPlane));
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
        QColor color = m_solve.fullyConstrained.contains(id) ? theme::kSketchConstrained : theme::kSketchFree;
        if (curve.construction) {
            color = theme::kSketchConstruction;
        }
        if (conflicted.contains(id)) {
            color = theme::kConflict;
        }
        if (m_selection.contains(id)) {
            color = theme::kGold;
        } else if (m_hover == id) {
            color = theme::kGold.lighter(130);
        }
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
        QColor color =
            m_solve.fullyConstrained.contains(id) || point.fixed ? theme::kSketchConstrained : theme::kSketchFree;
        if (id == sketch::kOriginId) {
            color = theme::kTitaniumLight;
        }
        if (m_selection.contains(id) || m_hover == id) {
            color = theme::kGold;
        }
        Handle(AIS_Shape) object = new AIS_Shape(BRepBuilderAPI_MakeVertex(sketch::toWorld(ax, point.pos)));
        object->Attributes()->SetPointAspect(new Prs3d_PointAspect(
            Aspect_TOM_BALL, occt(color), m_hover == id ? style::kMarkerMedium : style::kMarkerSmall));
        show(object, m_objects, false);
    }

    const std::optional<sketch::Constraint> pending =
        m_hasCursor ? pendingConstraint(m_cursor) : std::optional<sketch::Constraint>();
    if (pending) {
        const Id id = shown.addConstraint(pending->kind, pending->refs, pending->value);
        shown.constraint(id)->labelOffset = pending->labelOffset;
    }
    drawConstraints(shown, m_solve);
    m_view->redraw();
    emit stateChanged();
}

void SketchEditor::drawConstraints(const sketch::Sketch& s, const sketch::SolveResult& solve)
{
    const gp_Ax3 ax = plane();
    const gp_Pln pln(ax);
    const double wpp = m_view->worldPerPixel();
    m_drawnScale = wpp;
    const double textScale = m_view->devicePixelRatioF();
    const std::set<Id> conflicting(solve.conflicting.begin(), solve.conflicting.end());
    std::map<Id, int> glyphCount;

    Vec2 lowest{std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    Vec2 highest{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
    for (const auto& [id, point] : s.points()) {
        lowest = {std::min(lowest.x, point.pos.x), std::min(lowest.y, point.pos.y)};
        highest = {std::max(highest.x, point.pos.x), std::max(highest.y, point.pos.y)};
    }
    const Vec2 center = (lowest + highest) * 0.5;
    const auto outwardFlyout = [&](Vec2 first, Vec2 second, Vec2 measured) {
        const Vec2 side{-measured.y, measured.x};
        const double sign = dot(side, (first + second) * 0.5 - center) < 0.0 ? -1.0 : 1.0;
        return sign * wpp * kDimensionFlyoutPixels;
    };

    const auto colorFor = [&](Id id) {
        if (m_selection.contains(id)) {
            return theme::kGold;
        }
        return conflicting.contains(id) ? theme::kConflictText : theme::kTitaniumLight;
    };
    const auto placeGlyph = [&](Id constraint, Vec2 at, const QString& icon) {
        if (!m_display.constraints) {
            return;
        }
        const QColor color = conflicting.contains(constraint) ? theme::kConflict : theme::kTitaniumLight;
        Handle(AIS_InteractiveObject) badge =
            makeConstraintBadge(sketch::toWorld(ax, at), icon, color, m_selection.contains(constraint), textScale);
        show(badge, m_objects, true);
        m_view->aisContext()->SetSelectionSensitivity(badge, 0,
                                                      static_cast<int>(kConstraintBadgePixels * textScale / 2));
        m_constraintObjects.emplace(badge.get(), constraint);
    };
    const auto glyphNear = [&](Id constraint, Id entity, const QString& text) {
        Vec2 at;
        Vec2 side{0.0, 1.0};
        if (s.isCurve(entity)) {
            at = anchorOf(s, entity);
            const sketch::Curve* curve = s.curve(entity);
            if (curve->kind == CurveKind::Line) {
                const Vec2 d = normalized(s.point(curve->points[1])->pos - s.point(curve->points[0])->pos);
                side = {-d.y, d.x};
            } else {
                side = normalized(at - s.point(curve->points[0])->pos);
            }
        } else if (const sketch::Point* point = s.point(entity)) {
            at = point->pos;
            side = normalized(Vec2{1.0, 1.0});
        } else {
            return;
        }
        const int n = glyphCount[entity]++;
        placeGlyph(constraint, at + side * (wpp * (kGlyphPixels + kGlyphStepPixels * n)), text);
    };

    for (const auto& [id, constraint] : s.constraints()) {
        const QString glyph = glyphFor(constraint.kind);
        if (!glyph.isEmpty()) {
            switch (constraint.kind) {
            case ConstraintKind::Horizontal:
            case ConstraintKind::Vertical:
                if (constraint.refs.size() == 1) {
                    glyphNear(id, constraint.refs[0], glyph);
                } else {
                    const Vec2 a = s.point(constraint.refs[0])->pos;
                    const Vec2 b = s.point(constraint.refs[1])->pos;
                    placeGlyph(id, (a + b) * 0.5, glyph);
                }
                break;
            case ConstraintKind::PointOnCurve:
            case ConstraintKind::Midpoint:
                glyphNear(id, constraint.refs[0], glyph);
                break;
            case ConstraintKind::Symmetric:
                glyphNear(id, constraint.refs[0], glyph);
                glyphNear(id, constraint.refs[1], glyph);
                break;
            case ConstraintKind::Parallel:
            case ConstraintKind::Perpendicular:
            case ConstraintKind::Tangent:
            case ConstraintKind::Equal:
                for (Id ref : constraint.refs) {
                    glyphNear(id, ref, glyph);
                }
                break;
            case ConstraintKind::Coincident:
            case ConstraintKind::Distance:
            case ConstraintKind::HorizontalDistance:
            case ConstraintKind::VerticalDistance:
            case ConstraintKind::Radius:
            case ConstraintKind::Diameter:
            case ConstraintKind::Angle:
                break;
            }
            continue;
        }
        if (!sketch::isDimension(constraint.kind) || !m_display.dimensions) {
            continue;
        }

        Handle(PrsDim_Dimension) dimension;
        std::optional<double> flyout;
        QString text = formatValue(constraint.value);
        const auto world = [&](Vec2 p) { return sketch::toWorld(ax, p); };
        switch (constraint.kind) {
        case ConstraintKind::Distance: {
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
            continue;
        }
        const QColor color = colorFor(id);
        Handle(Prs3d_DimensionAspect) aspect = new Prs3d_DimensionAspect();
        aspect->MakeArrows3d(false);
        aspect->MakeText3d(false);
        aspect->SetCommonColor(occt(color));
        aspect->ArrowAspect()->SetLength(wpp * 10.0);
        aspect->TextAspect()->SetHeight(kDimensionTextPixels * textScale);
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
        show(dimension, m_objects, true);
        m_constraintObjects.emplace(dimension.get(), id);

        const DimensionLabel label = makeDimensionLabel(dimension->GetTextPosition(), text, color,
                                                        m_selection.contains(id), kDimensionTextPixels, textScale);
        show(label.object, m_objects, true);
        m_view->aisContext()->SetSelectionSensitivity(label.object, 0,
                                                      std::max(label.pixels.width(), label.pixels.height()) / 2);
        m_constraintObjects.emplace(label.object.get(), id);
    }

    for (const auto& [id, point] : s.points()) {
        if (point.fixed && id != sketch::kOriginId) {
            glyphNear(0, id, QStringLiteral("lock"));
        }
    }
    for (const auto& [id, curve] : s.curves()) {
        if (curve.fixed) {
            glyphNear(0, id, QStringLiteral("lock"));
        }
    }
}

void SketchEditor::redrawPreview()
{
    clearObjects(m_preview);
    if (!isActive() || !m_hasCursor || feature() == nullptr) {
        m_view->redraw();
        return;
    }
    const gp_Ax3 ax = plane();
    const auto world = [&](Vec2 p) { return sketch::toWorld(ax, p); };
    const auto addEdge = [&](const TopoDS_Shape& shape) {
        Handle(AIS_Shape) object = new AIS_Shape(shape);
        object->SetColor(occt(theme::kGold));
        object->SetWidth(style::kStrokeThin);
        object->Attributes()->SetWireAspect(
            new Prs3d_LineAspect(occt(theme::kGold), Aspect_TOL_DOT, style::kStrokeThin));
        show(object, m_preview, false);
    };
    const auto addSegment = [&](Vec2 a, Vec2 b) {
        if (sketch::distance(a, b) > 1e-9) {
            addEdge(BRepBuilderAPI_MakeEdge(world(a), world(b)));
        }
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
                    const double radius = m_view->worldPerPixel() * kAngleArcPixels;
                    const gp_Circ circle(gp_Ax2(world(m_first->pos), ax.Direction(), ax.XDirection()), radius);
                    addEdge(BRepBuilderAPI_MakeEdge(circle, std::min(start, start + sweep),
                                                    std::max(start, start + sweep)));
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
                addEdge(
                    BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(world(m_first->pos), ax.Direction(), ax.XDirection()), r)));
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
            addEdge(BRepBuilderAPI_MakeEdge(circle, a1, a2));
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
                addEdge(through < to ? BRepBuilderAPI_MakeEdge(circle, from, from + to).Shape()
                                     : BRepBuilderAPI_MakeEdge(circle, from + to, from + kTwoPi).Shape());
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
        TopoDS_Shape shape;
        if (piece->kind == CurveKind::Line) {
            if (sketch::distance(piece->start, piece->end) < 1e-9) {
                break;
            }
            shape = BRepBuilderAPI_MakeEdge(world(piece->start), world(piece->end));
        } else {
            const gp_Circ circle(gp_Ax2(world(piece->center), ax.Direction(), ax.XDirection()), piece->radius);
            const double end = piece->startAngle + wrapAngle(piece->endAngle - piece->startAngle);
            shape = piece->whole && piece->kind == CurveKind::Circle
                        ? BRepBuilderAPI_MakeEdge(circle).Shape()
                        : BRepBuilderAPI_MakeEdge(circle, piece->startAngle, end).Shape();
        }
        Handle(AIS_Shape) object = new AIS_Shape(shape);
        object->SetColor(occt(theme::kConflict));
        object->SetWidth(style::kStrokeBold);
        show(object, m_preview, false);
        break;
    }
    case Tool::Fillet:
        if (const std::optional<Id> corner = cornerAt(cursor)) {
            Handle(AIS_Shape) marker = new AIS_Shape(BRepBuilderAPI_MakeVertex(world(current().point(*corner)->pos)));
            marker->Attributes()->SetPointAspect(
                new Prs3d_PointAspect(Aspect_TOM_O_POINT, occt(theme::kGold), style::kMarkerMedium));
            show(marker, m_preview, false);
        }
        break;
    case Tool::Mirror:
        if (m_mirrorAxis != 0 && current().isCurve(m_mirrorAxis)) {
            const Hit hit = curveAt(cursor);
            sketch::Sketch probe = current();
            if (hit.kind == Hit::Kind::Curve) {
                if (const auto copies = sketch::addMirror(probe, {hit.id}, m_mirrorAxis)) {
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
    case Tool::Dimension:
    case Tool::Coincident:
    case Tool::Horizontal:
    case Tool::Vertical:
    case Tool::Parallel:
    case Tool::Perpendicular:
    case Tool::Tangent:
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
            if (source && sketch::distance(*source, cursor) > 1e-9) {
                Handle(AIS_Shape) guide = new AIS_Shape(BRepBuilderAPI_MakeEdge(world(*source), world(cursor)));
                guide->SetColor(occt(theme::kTitaniumLight));
                guide->SetWidth(kInferenceLineWidth);
                guide->Attributes()->SetWireAspect(
                    new Prs3d_LineAspect(occt(theme::kTitaniumLight), Aspect_TOL_DASH, kInferenceLineWidth));
                show(guide, m_preview, false);
            }
        }
        Handle(AIS_Shape) crosshair = new AIS_Shape(BRepBuilderAPI_MakeVertex(world(cursor)));
        crosshair->Attributes()->SetPointAspect(
            new Prs3d_PointAspect(Aspect_TOM_PLUS, occt(theme::kGold), kCursorMarkerScale));
        show(crosshair, m_preview, false);
    }
    if (snapUsed && (m_cursorSnap.point != 0 || m_cursorSnap.midpointOf != 0 || m_cursorSnap.onCurve != 0)) {
        Handle(AIS_Shape) marker = new AIS_Shape(BRepBuilderAPI_MakeVertex(world(cursor)));
        const bool midpoint = m_cursorSnap.midpointOf != 0;
        const Aspect_TypeOfMarker type = midpoint ? Aspect_TOM_O_STAR : Aspect_TOM_O_POINT;
        marker->Attributes()->SetPointAspect(
            new Prs3d_PointAspect(type, occt(theme::kGold), midpoint ? kMidpointMarkerScale : kSnapMarkerScale));
        show(marker, m_preview, false);
    }
    placeInputs(cursor);
    m_view->redraw();
}

} // namespace kalve137::app
