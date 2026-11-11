// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/session/design_session.h"
#include "app/sketch/sketch_editor_detail.h"

#include "sketch/draft.h"
#include "sketch/profiles.h"
#include "viewer/occt_view_widget.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <utility>

namespace kalve137::app {

using namespace sketch_editor_detail;

namespace {

bool usesPoint(const sketch::Curve& curve, Id point)
{
    return std::ranges::find(curve.points, point) != curve.points.end();
}

} // namespace

std::optional<Vec2> SketchEditor::toSketch(QPointF pos) const
{
    const std::optional<gp_Pnt> point = m_view->pickOnPlane(pos, plane());
    if (!point) {
        return std::nullopt;
    }
    return sketch::toSketch(plane(), *point);
}

double SketchEditor::tolerance() const
{
    return m_view->worldPerPixel() * kPickPixels;
}

SketchEditor::Snap SketchEditor::snapAt(Vec2 pos, Id ignorePoint) const
{
    const sketch::Sketch& s = current();
    const double tol = tolerance();
    Snap snap;
    snap.pos = pos;

    double best = tol;
    for (const auto& [id, point] : s.points()) {
        const double d = sketch::distance(point.pos, pos);
        if (id != ignorePoint && d < best) {
            best = d;
            snap.point = id;
            snap.pos = point.pos;
        }
    }
    if (snap.point != 0) {
        return snap;
    }
    for (const auto& [id, curve] : s.curves()) {
        if (curve.kind != CurveKind::Line) {
            continue;
        }
        const Vec2 mid = *s.midpoint(id);
        const double d = sketch::distance(mid, pos);
        if (d < best) {
            best = d;
            snap.midpointOf = id;
            snap.pos = mid;
        }
    }
    if (snap.midpointOf != 0) {
        return snap;
    }
    const Hit hit = geometryAt(pos);
    if (hit.kind == Hit::Kind::Curve) {
        const sketch::Curve* curve = s.curve(hit.id);
        switch (curve->kind) {
        case CurveKind::Line:
            snap.pos = closestOnSegment(pos, s.point(curve->points[0])->pos, s.point(curve->points[1])->pos);
            break;
        case CurveKind::Circle:
        case CurveKind::Arc: {
            const Vec2 c = s.point(curve->points[0])->pos;
            snap.pos = c + normalized(pos - c) * curve->radius;
            break;
        }
        }
        snap.onCurve = hit.id;
        return snap;
    }
    if (const double step = m_view->gridStep(); m_gridSnap && step > 0.0) {
        snap.pos = sketch::snapToGrid(pos, step);
        snap.onGrid = true;
    }
    const double inferTolerance = m_view->worldPerPixel() * kInferencePixels;
    double bestX = inferTolerance;
    double bestY = inferTolerance;
    const auto align = [&](Vec2 source, double offset, double& best, std::optional<Vec2>& aligned) {
        const bool nearer =
            aligned && offset == best && sketch::distance(source, pos) < sketch::distance(*aligned, pos);
        if (offset < best || nearer) {
            best = offset;
            aligned = source;
        }
    };
    const auto consider = [&](Vec2 source) {
        align(source, std::abs(source.x - pos.x), bestX, snap.alignedX);
        align(source, std::abs(source.y - pos.y), bestY, snap.alignedY);
    };
    for (const auto& [id, point] : s.points()) {
        if (id != ignorePoint) {
            consider(point.pos);
        }
    }
    for (const auto& [id, curve] : s.curves()) {
        if (curve.kind == CurveKind::Line && !usesPoint(curve, ignorePoint)) {
            consider(*s.midpoint(id));
        }
    }
    if (m_first) {
        consider(m_first->pos);
    }
    if (snap.alignedX) {
        snap.pos.x = snap.alignedX->x;
    }
    if (snap.alignedY) {
        snap.pos.y = snap.alignedY->y;
    }
    extendAlongLine(pos, ignorePoint, snap);
    return snap;
}

void SketchEditor::extendAlongLine(Vec2 pos, Id ignorePoint, Snap& snap) const
{
    const sketch::Sketch& s = current();
    double best = m_view->worldPerPixel() * kInferencePixels;
    std::optional<std::pair<Vec2, Vec2>> found;
    for (const auto& [id, curve] : s.curves()) {
        if (curve.kind != CurveKind::Line || usesPoint(curve, ignorePoint)) {
            continue;
        }
        const Vec2 a = s.point(curve.points[0])->pos;
        const Vec2 b = s.point(curve.points[1])->pos;
        const double span = length(b - a);
        if (span <= 0.0) {
            continue;
        }
        const Vec2 along = (b - a) * (1.0 / span);
        const double t = dot(pos - a, along);
        if (t >= 0.0 && t <= span) {
            continue;
        }
        const double offset = std::abs(along.x * (pos.y - a.y) - along.y * (pos.x - a.x));
        if (offset < best) {
            best = offset;
            found = {t < 0.0 ? a : b, along};
        }
    }
    if (!found) {
        return;
    }
    const auto [end, along] = *found;
    if (snap.alignedX && std::abs(along.x) > kMinCrossing) {
        snap.pos = end + along * ((snap.alignedX->x - end.x) / along.x);
        snap.alignedY.reset();
    } else if (snap.alignedY && std::abs(along.y) > kMinCrossing) {
        snap.pos = end + along * ((snap.alignedY->y - end.y) / along.y);
        snap.alignedX.reset();
    } else {
        snap.pos = end + along * dot(pos - end, along);
        snap.alignedX.reset();
        snap.alignedY.reset();
    }
    snap.extendedFrom = end;
    snap.onGrid = false;
}

Id SketchEditor::placePoint(sketch::Sketch& sketch, const Snap& snap)
{
    if (snap.point != 0) {
        return snap.point;
    }
    const Id id = sketch.addPoint(snap.pos);
    if (snap.midpointOf != 0) {
        sketch.addConstraint(ConstraintKind::Midpoint, {id, snap.midpointOf});
    } else if (snap.onCurve != 0) {
        sketch.addConstraint(ConstraintKind::PointOnCurve, {id, snap.onCurve});
    }
    return id;
}

SketchEditor::Hit SketchEditor::geometryAt(Vec2 pos) const
{
    const sketch::Sketch& s = current();
    const double tol = tolerance();
    Hit hit;

    double best = tol;
    for (const auto& [id, point] : s.points()) {
        const double d = sketch::distance(point.pos, pos);
        if (d < best) {
            best = d;
            hit = {Hit::Kind::Point, id};
        }
    }
    if (hit.kind != Hit::Kind::None) {
        return hit;
    }
    return curveAt(pos);
}

SketchEditor::Hit SketchEditor::curveAt(Vec2 pos) const
{
    const sketch::Sketch& s = current();
    const double tol = tolerance();
    Hit hit;
    double best = tol;
    for (const auto& [id, curve] : s.curves()) {
        double d = tol;
        switch (curve.kind) {
        case CurveKind::Line: {
            const Vec2 a = s.point(curve.points[0])->pos;
            const Vec2 b = s.point(curve.points[1])->pos;
            d = sketch::distance(closestOnSegment(pos, a, b), pos);
            break;
        }
        case CurveKind::Circle:
            d = std::abs(sketch::distance(s.point(curve.points[0])->pos, pos) - curve.radius);
            break;
        case CurveKind::Arc: {
            const Vec2 c = s.point(curve.points[0])->pos;
            const double start = angleOf(s.point(curve.points[1])->pos - c);
            const double end = angleOf(s.point(curve.points[2])->pos - c);
            if (angleInArc(angleOf(pos - c), start, end)) {
                d = std::abs(sketch::distance(c, pos) - curve.radius);
            }
            break;
        }
        }
        if (d < best) {
            best = d;
            hit = {Hit::Kind::Curve, id};
        }
    }
    return hit;
}

std::optional<Id> SketchEditor::detectedConstraint() const
{
    const Handle(AIS_InteractiveContext)& context = m_view->aisContext();
    if (!context->HasDetected()) {
        return std::nullopt;
    }
    const Handle(AIS_InteractiveObject) object = context->DetectedInteractive();
    auto it = m_constraintObjects.find(object.get());
    if (it == m_constraintObjects.end() || current().constraint(it->second) == nullptr) {
        return std::nullopt;
    }
    return it->second;
}

SketchEditor::Hit SketchEditor::hitAt(QPointF pos) const
{
    const std::optional<Vec2> p = toSketch(pos);
    const Hit geometry = p ? geometryAt(*p) : Hit{};
    if (geometry.kind == Hit::Kind::Point) {
        return geometry;
    }
    if (const std::optional<Id> constraint = detectedConstraint()) {
        return {Hit::Kind::Constraint, *constraint};
    }
    return geometry;
}

} // namespace kalve137::app
