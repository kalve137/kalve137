// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/sketch/sketch_editor_detail.h"
#include "app/sketch/sketch_inputs.h"

#include "sketch/dimension.h"
#include "sketch/draft.h"
#include "sketch/edit.h"
#include "sketch/profiles.h"
#include "viewer/occt_view_widget.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace kalve137::app {

using namespace sketch_editor_detail;

void SketchEditor::clickLine(Snap snap)
{
    if (!m_first) {
        m_first = snap;
        openInputs();
        return;
    }
    if (anyHeld()) {
        snap = Snap{draftLineEnd(snap.pos)};
    }
    const Vec2 delta = snap.pos - m_first->pos;
    if (length(delta) < tolerance() * 0.5) {
        return;
    }
    const bool closesLoop = snap.point != 0 && snap.point == m_first->point;
    const std::optional<double> heldLength = heldPositive(0);
    const std::optional<double> heldAngle = heldPositive(1);

    struct Built {
        sketch::Sketch sketch;
        Id line = 0;
        Id end = 0;
    };
    const auto build = [&](bool autoAxis, bool dimensions) {
        Built built{current()};
        sketch::Sketch& candidate = built.sketch;
        const Id start = placePoint(candidate, *m_first);
        built.end = placePoint(candidate, snap);
        built.line = candidate.addLine(start, built.end);
        if (autoAxis && !heldAngle) {
            const double degrees = std::abs(std::atan2(delta.y, delta.x)) * 180.0 / std::numbers::pi;
            if (degrees < kAutoAxisDegrees || degrees > 180.0 - kAutoAxisDegrees) {
                candidate.addConstraint(ConstraintKind::Horizontal, {built.line});
            } else if (std::abs(degrees - 90.0) < kAutoAxisDegrees) {
                candidate.addConstraint(ConstraintKind::Vertical, {built.line});
            }
        }
        if (dimensions && heldLength) {
            candidate.addConstraint(ConstraintKind::Distance, {built.line}, *heldLength);
        }
        const sketch::Curve* previous = candidate.curve(m_chainLine);
        if (autoAxis && heldAngle && previous == nullptr) {
            const double quarter = std::remainder(*heldAngle, 90.0);
            if (std::abs(quarter) < 1e-6) {
                const bool vertical = std::abs(std::remainder(*heldAngle, 180.0)) > 1.0;
                candidate.addConstraint(vertical ? ConstraintKind::Vertical : ConstraintKind::Horizontal, {built.line});
            }
        }
        if (dimensions && heldAngle && previous != nullptr) {
            const Vec2 d1 = candidate.point(previous->points[1])->pos - candidate.point(previous->points[0])->pos;
            const double angle =
                std::abs(std::atan2(d1.x * delta.y - d1.y * delta.x, dot(d1, delta))) * 180.0 / std::numbers::pi;
            if (angle > 1e-6 && angle < 180.0 - 1e-6) {
                candidate.addConstraint(ConstraintKind::Angle, {m_chainLine, built.line}, angle);
            }
        }
        return built;
    };
    const auto acceptable = [this](sketch::Sketch probe) {
        const sketch::SolveResult result = sketch::solve(probe);
        return result.solved && result.conflicting.empty() && result.redundant.size() <= m_solve.redundant.size();
    };

    Built built = build(true, true);
    if (!acceptable(built.sketch)) {
        built = build(false, true);
    }
    if (!acceptable(built.sketch)) {
        built = build(false, false);
    }
    if (!commit(tr("Line"), std::move(built.sketch))) {
        return;
    }
    if (closesLoop) {
        resetProgress();
        return;
    }
    Snap next;
    next.point = built.end;
    next.pos = current().point(built.end)->pos;
    m_first = next;
    m_chainLine = built.line;
    openInputs();
}

void SketchEditor::clickRectangle(Snap snap)
{
    if (!m_first) {
        m_first = snap;
        openInputs();
        return;
    }
    if (anyHeld()) {
        snap = Snap{draftCorner(snap.pos)};
    }
    const Vec2 a = m_first->pos;
    const Vec2 c = snap.pos;
    if (std::abs(c.x - a.x) < tolerance() * 0.5 || std::abs(c.y - a.y) < tolerance() * 0.5) {
        return;
    }
    const auto build = [&](bool dimensions) {
        sketch::Sketch candidate = current();
        const Id p0 = placePoint(candidate, *m_first);
        const Id p1 = candidate.addPoint({c.x, a.y});
        const Id p2 = placePoint(candidate, snap);
        const Id p3 = candidate.addPoint({a.x, c.y});
        const Id bottom = candidate.addLine(p0, p1);
        const Id right = candidate.addLine(p1, p2);
        const Id top = candidate.addLine(p2, p3);
        const Id left = candidate.addLine(p3, p0);
        candidate.addConstraint(ConstraintKind::Horizontal, {bottom});
        candidate.addConstraint(ConstraintKind::Horizontal, {top});
        candidate.addConstraint(ConstraintKind::Vertical, {right});
        candidate.addConstraint(ConstraintKind::Vertical, {left});
        if (dimensions) {
            if (const std::optional<double> width = heldPositive(0)) {
                candidate.addConstraint(ConstraintKind::Distance, {bottom}, *width);
            }
            if (const std::optional<double> height = heldPositive(1)) {
                candidate.addConstraint(ConstraintKind::Distance, {right}, *height);
            }
        }
        return candidate;
    };
    if (commit(tr("Rectangle"), withDimensionsIfSolvable(build, m_solve.redundant.size()))) {
        resetProgress();
    }
}

void SketchEditor::clickCircle(Snap snap)
{
    if (!m_first) {
        m_first = snap;
        openInputs();
        return;
    }
    const double radius = draftRadius(snap.pos);
    if (radius < tolerance() * 0.5) {
        return;
    }
    const std::optional<double> diameter = heldPositive(0);
    const auto build = [&](bool dimensions) {
        sketch::Sketch candidate = current();
        const Id circle = candidate.addCircle(placePoint(candidate, *m_first), radius);
        if (dimensions && diameter) {
            candidate.addConstraint(ConstraintKind::Diameter, {circle}, *diameter);
        }
        return candidate;
    };
    if (commit(tr("Circle"), withDimensionsIfSolvable(build, m_solve.redundant.size()))) {
        resetProgress();
    }
}

void SketchEditor::openInputs()
{
    using Kind = SketchInputs::Kind;
    switch (m_tool) {
    case Tool::Line:
        m_inputs->open({Kind::Length, Kind::Angle});
        return;
    case Tool::Rectangle:
        m_inputs->open({Kind::Length, Kind::Length});
        return;
    case Tool::Circle:
        m_inputs->open({Kind::Length});
        return;
    case Tool::CenterRectangle:
        m_inputs->open({Kind::Length, Kind::Length});
        return;
    case Tool::Polygon:
        m_inputs->open({Kind::Length, Kind::Count});
        return;
    case Tool::Select:
    case Tool::Arc:
    case Tool::ArcThrough:
    case Tool::Point:
    case Tool::Trim:
    case Tool::Fillet:
    case Tool::Mirror:
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
        m_inputs->close();
        return;
    }
}

void SketchEditor::onInputsAccepted()
{
    if (!isActive() || !m_first) {
        return;
    }
    const Snap at = anyHeld() ? Snap{m_cursorSnap.pos} : m_cursorSnap;
    switch (m_tool) {
    case Tool::Line:
        clickLine(at);
        resetProgress();
        break;
    case Tool::Rectangle:
        clickRectangle(at);
        break;
    case Tool::Circle:
        clickCircle(at);
        break;
    case Tool::CenterRectangle:
        clickCenterRectangle(at);
        break;
    case Tool::Polygon:
        clickPolygon(at);
        break;
    case Tool::Select:
    case Tool::Arc:
    case Tool::ArcThrough:
    case Tool::Point:
    case Tool::Trim:
    case Tool::Fillet:
    case Tool::Mirror:
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
        return;
    }
    redrawPreview();
    emit stateChanged();
}

bool SketchEditor::anyHeld() const
{
    return m_inputs->held(0).has_value() || m_inputs->held(1).has_value();
}

std::optional<double> SketchEditor::heldPositive(std::size_t index) const
{
    const std::optional<double> value = m_inputs->held(index);
    return value && *value > 0.0 ? value : std::nullopt;
}

std::optional<Vec2> SketchEditor::chainDirection() const
{
    if (!m_first || m_chainLine == 0 || feature() == nullptr) {
        return std::nullopt;
    }
    const sketch::Sketch& s = current();
    const sketch::Curve* line = s.curve(m_chainLine);
    if (line == nullptr || line->kind != CurveKind::Line) {
        return std::nullopt;
    }
    const Vec2 a = s.point(line->points[0])->pos;
    const Vec2 b = s.point(line->points[1])->pos;
    return line->points[1] == m_first->point ? b - a : a - b;
}

Vec2 SketchEditor::protractorReference() const
{
    return chainDirection().value_or(Vec2{-1.0, 0.0});
}

Vec2 SketchEditor::draftLineEnd(Vec2 cursor) const
{
    if (!m_first) {
        return cursor;
    }
    const Vec2 reference = protractorReference();
    const std::optional<double> angle = heldPositive(1);
    const double limit = chainDirection() ? 180.0 - 1e-9 : 180.0;
    return sketch::lineEnd(m_first->pos, cursor, reference, heldPositive(0),
                           angle && *angle <= limit ? angle : std::nullopt);
}

Vec2 SketchEditor::draftCorner(Vec2 cursor) const
{
    if (!m_first) {
        return cursor;
    }
    const Vec2 a = m_first->pos;
    Vec2 c = cursor;
    if (const std::optional<double> width = heldPositive(0)) {
        c.x = a.x + (cursor.x >= a.x ? *width : -*width);
    }
    if (const std::optional<double> height = heldPositive(1)) {
        c.y = a.y + (cursor.y >= a.y ? *height : -*height);
    }
    return c;
}

double SketchEditor::draftRadius(Vec2 cursor) const
{
    if (const std::optional<double> diameter = heldPositive(0)) {
        return *diameter / 2.0;
    }
    return m_first ? sketch::distance(m_first->pos, cursor) : 0.0;
}

void SketchEditor::placeInputs(Vec2 cursor)
{
    if (!m_inputs->isOpen() || !m_first) {
        return;
    }
    const gp_Ax3 ax = plane();
    const auto screen = [&](Vec2 p) { return m_view->toScreen(sketch::toWorld(ax, p)); };
    const auto unitScreen = [](QPointF v) {
        const double n = std::hypot(v.x(), v.y());
        return n > 1e-9 ? v / n : QPointF(0.0, -1.0);
    };
    const auto beside = [&](std::size_t index, double value, Vec2 a, Vec2 b, Vec2 inside) {
        const QPointF mid = screen((a + b) * 0.5);
        const QPointF d = screen(b) - screen(a);
        QPointF normal = unitScreen(QPointF(-d.y(), d.x()));
        const QPointF toInside = screen(inside) - mid;
        if (normal.x() * toInside.x() + normal.y() * toInside.y() > 0.0) {
            normal = -normal;
        }
        m_inputs->setLive(index, value, mid + normal * kInputGapPixels, normal);
    };

    const Vec2 start = m_first->pos;
    switch (m_tool) {
    case Tool::Line: {
        const Vec2 end = draftLineEnd(cursor);
        const std::optional<Vec2> previous = chainDirection();
        const Vec2 inside = previous ? start - *previous : start + Vec2{0.0, -1.0};
        beside(0, sketch::distance(start, end), start, end, inside);
        const Vec2 reference = protractorReference();
        const Vec2 back = normalized(reference * -1.0);
        const Vec2 dir = sketch::distance(start, end) > 1e-9 ? normalized(end - start) : back;
        Vec2 bisector = back + dir;
        if (length(bisector) < 1e-6) {
            bisector = {-back.y, back.x};
        }
        const QPointF joint = screen(start);
        const QPointF along = unitScreen(screen(start + normalized(bisector)) - joint);
        m_inputs->setLive(1, sketch::interiorAngle(reference, dir), joint + along * (kAngleArcPixels + kInputGapPixels),
                          along);
        return;
    }
    case Tool::Rectangle: {
        const Vec2 c = draftCorner(cursor);
        const Vec2 center = (start + c) * 0.5;
        beside(0, std::abs(c.x - start.x), start, {c.x, start.y}, center);
        beside(1, std::abs(c.y - start.y), {c.x, start.y}, c, center);
        return;
    }
    case Tool::Circle: {
        const double radius = draftRadius(cursor);
        const Vec2 dir = sketch::distance(start, cursor) > 1e-9 ? normalized(cursor - start) : Vec2{1.0, 0.0};
        const QPointF rim = screen(start + dir * radius);
        const QPointF out = unitScreen(rim - screen(start));
        m_inputs->setLive(0, radius * 2.0, rim + out * kInputGapPixels, out);
        return;
    }
    case Tool::CenterRectangle: {
        const Vec2 c = draftCenterCorner(cursor);
        const Vec2 opposite = start * 2.0 - c;
        beside(0, std::abs(c.x - start.x) * 2.0, {opposite.x, c.y}, c, start);
        beside(1, std::abs(c.y - start.y) * 2.0, {c.x, opposite.y}, c, start);
        return;
    }
    case Tool::Polygon: {
        const Vec2 corner = draftPolygonCorner(cursor);
        const QPointF rim = screen(corner);
        const QPointF out = unitScreen(rim - screen(start));
        m_inputs->setLive(0, sketch::distance(start, corner) * 2.0, rim + out * kInputGapPixels, out);
        m_inputs->setLive(1, polygonSides(), screen(start) + QPointF(0.0, kInputOffsetPixels));
        return;
    }
    case Tool::Select:
    case Tool::Arc:
    case Tool::ArcThrough:
    case Tool::Point:
    case Tool::Trim:
    case Tool::Fillet:
    case Tool::Mirror:
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
        return;
    }
}

void SketchEditor::clickArc(const Snap& snap)
{
    if (!m_first) {
        m_first = snap;
        return;
    }
    if (!m_second) {
        if (sketch::distance(m_first->pos, snap.pos) < tolerance() * 0.5) {
            return;
        }
        m_second = snap;
        m_arcSweep = 0.0;
        m_arcLastAngle = angleOf(snap.pos - m_first->pos);
        return;
    }
    if (std::abs(m_arcSweep) < 1e-3) {
        return;
    }
    const Vec2 c = m_first->pos;
    const double radius = sketch::distance(c, m_second->pos);
    const double startAngle = angleOf(m_second->pos - c);
    const double endAngle = startAngle + m_arcSweep;
    Snap endSnap = snap;
    if (endSnap.point == 0) {
        endSnap = Snap{};
        endSnap.pos = c + Vec2{std::cos(endAngle), std::sin(endAngle)} * radius;
    }

    sketch::Sketch candidate = current();
    const Id center = placePoint(candidate, *m_first);
    const Id start = placePoint(candidate, *m_second);
    const Id end = placePoint(candidate, endSnap);
    if (m_arcSweep > 0.0) {
        candidate.addArc(center, start, end);
    } else {
        candidate.addArc(center, end, start);
    }
    if (commit(tr("Arc"), std::move(candidate))) {
        resetProgress();
    }
}

void SketchEditor::clickTrim(Vec2 pos)
{
    const Hit hit = curveAt(pos);
    if (hit.kind != Hit::Kind::Curve) {
        emit message(hint());
        return;
    }
    std::optional<sketch::Sketch> trimmed = sketch::trim(current(), hit.id, pos);
    if (!trimmed) {
        return;
    }
    m_selection.clear();
    m_hover = 0;
    commit(tr("Trim"), std::move(*trimmed));
}

std::optional<Id> SketchEditor::cornerAt(Vec2 pos) const
{
    const sketch::Sketch& s = current();
    std::optional<Id> best;
    double bestDistance = tolerance() * 1.5;
    for (const auto& [id, point] : s.points()) {
        const double d = sketch::distance(point.pos, pos);
        if (d >= bestDistance) {
            continue;
        }
        const std::vector<Id> curves = s.curvesUsing(id);
        const bool twoLines = curves.size() == 2 && std::ranges::all_of(curves, [&](Id curve) {
                                  return s.curve(curve)->kind == CurveKind::Line;
                              });
        if (twoLines) {
            best = id;
            bestDistance = d;
        }
    }
    return best;
}

void SketchEditor::clickFillet(QPointF screen, Vec2 pos)
{
    const std::optional<Id> corner = cornerAt(pos);
    if (!corner) {
        emit message(hint());
        return;
    }
    const Id point = *corner;
    promptValue(screen, m_filletRadius, core::Quantity::Length, false, [this, point](double radius) {
        std::optional<sketch::Sketch> rounded = sketch::filletCorner(current(), point, radius);
        if (!rounded) {
            emit message(tr("A %1 mm radius doesn't fit between those lines").arg(formatValue(radius)));
            return;
        }
        m_filletRadius = radius;
        m_selection.clear();
        commit(tr("Sketch Fillet"), std::move(*rounded));
    });
}

void SketchEditor::clickDimension(QPointF pos, const Hit& hit)
{
    const sketch::Sketch& s = current();
    const std::optional<Vec2> at = toSketch(pos);
    if (!at) {
        return;
    }
    if (hit.kind == Hit::Kind::Constraint && !m_pending) {
        m_picks.clear();
        editDimension(hit.id, pos);
        return;
    }

    if (m_picks.empty()) {
        if (m_pending) {
            placeDimension(pos, *at);
            return;
        }
        if (hit.kind == Hit::Kind::None) {
            return;
        }
        if (hit.kind == Hit::Kind::Curve) {
            const sketch::Curve* curve = s.curve(hit.id);
            switch (curve->kind) {
            case CurveKind::Line:
                m_picks.push_back(hit);
                startDimension(ConstraintKind::Distance, {hit.id},
                               sketch::distance(s.point(curve->points[0])->pos, s.point(curve->points[1])->pos));
                return;
            case CurveKind::Circle:
                startDimension(ConstraintKind::Diameter, {hit.id}, curve->radius * 2.0);
                return;
            case CurveKind::Arc:
                startDimension(ConstraintKind::Radius, {hit.id}, curve->radius);
                return;
            }
        }
        m_picks.push_back(hit);
        return;
    }

    const Hit first = m_picks.front();
    m_picks.clear();
    if (first.kind == Hit::Kind::Curve) {
        const sketch::Curve* line = s.curve(first.id);
        if (hit.kind == Hit::Kind::Curve && hit.id != first.id && s.curve(hit.id)->kind == CurveKind::Line) {
            const sketch::Curve* other = s.curve(hit.id);
            const Vec2 d1 = s.point(line->points[1])->pos - s.point(line->points[0])->pos;
            const Vec2 d2 = s.point(other->points[1])->pos - s.point(other->points[0])->pos;
            const double angle =
                std::abs(std::atan2(d1.x * d2.y - d1.y * d2.x, dot(d1, d2))) * 180.0 / std::numbers::pi;
            if (angle < 1e-6 || angle > 180.0 - 1e-6) {
                m_pending.reset();
                emit message(tr("Parallel lines have no angle, use Parallel or a distance"));
                return;
            }
            startDimension(ConstraintKind::Angle, {first.id, hit.id}, angle);
            return;
        }
        if (hit.kind == Hit::Kind::Point && std::ranges::find(line->points, hit.id) == line->points.end()) {
            const Vec2 p = s.point(hit.id)->pos;
            const Vec2 a = s.point(line->points[0])->pos;
            const Vec2 dir = normalized(s.point(line->points[1])->pos - a);
            const double d = std::abs((p.x - a.x) * dir.y - (p.y - a.y) * dir.x);
            startDimension(ConstraintKind::Distance, {hit.id, first.id}, d);
            return;
        }
        if (!m_pending) {
            startDimension(ConstraintKind::Distance, {first.id},
                           sketch::distance(s.point(line->points[0])->pos, s.point(line->points[1])->pos));
        }
        placeDimension(pos, *at);
        return;
    }

    if (hit.kind == Hit::Kind::Curve && s.curve(hit.id)->kind == CurveKind::Line) {
        m_picks = {hit};
        clickDimension(pos, first);
        return;
    }
    if (hit.kind != Hit::Kind::Point || hit.id == first.id) {
        return;
    }
    const Vec2 a = s.point(first.id)->pos;
    const Vec2 b = s.point(hit.id)->pos;
    const double eps = tolerance() * 0.1;
    if (std::abs(a.y - b.y) <= eps) {
        startDimension(ConstraintKind::HorizontalDistance, {first.id, hit.id}, std::abs(b.x - a.x));
        return;
    }
    if (std::abs(a.x - b.x) <= eps) {
        startDimension(ConstraintKind::VerticalDistance, {first.id, hit.id}, std::abs(b.y - a.y));
        return;
    }
    startDimension(ConstraintKind::Distance, {first.id, hit.id}, sketch::distance(a, b), true);
}

void SketchEditor::startDimension(ConstraintKind kind, std::vector<Id> refs, double value, bool axisFromCursor)
{
    m_pending = PendingDimension{kind, std::move(refs), value, axisFromCursor};
    redraw();
}

std::optional<sketch::Constraint> SketchEditor::pendingConstraint(Vec2 cursor) const
{
    if (!m_pending) {
        return std::nullopt;
    }
    const sketch::Sketch& s = current();
    sketch::Constraint constraint{m_pending->kind, m_pending->refs, m_pending->value};
    if (m_pending->axisFromCursor) {
        const sketch::Point* first = s.point(constraint.refs[0]);
        const sketch::Point* second = s.point(constraint.refs[1]);
        if (first == nullptr || second == nullptr) {
            return std::nullopt;
        }
        const Vec2 a = first->pos;
        const Vec2 b = second->pos;
        constraint.kind = sketch::pointDistanceKind(a, b, cursor);
        if (constraint.kind == ConstraintKind::HorizontalDistance) {
            constraint.value = std::abs(b.x - a.x);
        } else if (constraint.kind == ConstraintKind::VerticalDistance) {
            constraint.value = std::abs(b.y - a.y);
        } else {
            constraint.value = sketch::distance(a, b);
        }
    }
    const std::optional<Vec2> anchor = sketch::dimensionAnchor(s, constraint);
    if (!anchor) {
        return std::nullopt;
    }
    constraint.labelOffset = cursor - *anchor;
    return constraint;
}

void SketchEditor::placeDimension(QPointF pos, Vec2 at)
{
    const std::optional<sketch::Constraint> constraint = pendingConstraint(at);
    m_pending.reset();
    m_picks.clear();
    if (constraint) {
        addDimension(*constraint, pos);
    }
}

void SketchEditor::clickConstraint(const Hit& hit)
{
    const sketch::Sketch& s = current();
    const auto isLine = [&](const Hit& h) {
        return h.kind == Hit::Kind::Curve && s.curve(h.id)->kind == CurveKind::Line;
    };
    const auto isRoundHit = [&](const Hit& h) { return h.kind == Hit::Kind::Curve && isRound(s.curve(h.id)->kind); };
    const auto apply = [&](ConstraintKind kind, std::vector<Id> refs, const QString& label) {
        m_picks.clear();
        sketch::Sketch candidate = s;
        candidate.addConstraint(kind, std::move(refs));
        commit(label, std::move(candidate));
    };

    if (m_tool == Tool::Fix) {
        sketch::Sketch candidate = s;
        if (hit.kind == Hit::Kind::Point && hit.id != sketch::kOriginId) {
            sketch::Point* point = candidate.point(hit.id);
            point->fixed = !point->fixed;
        } else if (hit.kind == Hit::Kind::Curve) {
            sketch::Curve* curve = candidate.curve(hit.id);
            curve->fixed = !curve->fixed;
        } else {
            return;
        }
        commit(tr("Fix"), std::move(candidate));
        return;
    }

    if (m_tool == Tool::Symmetric) {
        if (m_picks.size() < 2) {
            if (hit.kind == Hit::Kind::Point && (m_picks.empty() || m_picks.front().id != hit.id)) {
                m_picks.push_back(hit);
            } else {
                emit message(hint());
            }
            return;
        }
        if (!isLine(hit)) {
            emit message(tr("Click the line the two points mirror across"));
            return;
        }
        apply(ConstraintKind::Symmetric, {m_picks[0].id, m_picks[1].id, hit.id}, tr("Symmetric"));
        return;
    }

    if ((m_tool == Tool::Horizontal || m_tool == Tool::Vertical) && m_picks.empty() && isLine(hit)) {
        apply(m_tool == Tool::Horizontal ? ConstraintKind::Horizontal : ConstraintKind::Vertical, {hit.id},
              m_tool == Tool::Horizontal ? tr("Horizontal") : tr("Vertical"));
        return;
    }

    if (m_picks.empty()) {
        m_picks.push_back(hit);
        return;
    }
    const Hit first = m_picks.front();
    m_picks.clear();
    if (first.id == hit.id) {
        return;
    }
    const auto both = [&](auto predicate) { return predicate(first) && predicate(hit); };
    const auto isPointHit = [](const Hit& h) { return h.kind == Hit::Kind::Point; };

    switch (m_tool) {
    case Tool::Horizontal:
    case Tool::Vertical:
        if (both(isPointHit)) {
            apply(m_tool == Tool::Horizontal ? ConstraintKind::Horizontal : ConstraintKind::Vertical,
                  {first.id, hit.id}, m_tool == Tool::Horizontal ? tr("Horizontal") : tr("Vertical"));
            return;
        }
        break;
    case Tool::Coincident:
        if (both(isPointHit)) {
            apply(ConstraintKind::Coincident, {first.id, hit.id}, tr("Coincident"));
            return;
        }
        if (isPointHit(first) && hit.kind == Hit::Kind::Curve) {
            apply(ConstraintKind::PointOnCurve, {first.id, hit.id}, tr("Coincident"));
            return;
        }
        if (first.kind == Hit::Kind::Curve && isPointHit(hit)) {
            apply(ConstraintKind::PointOnCurve, {hit.id, first.id}, tr("Coincident"));
            return;
        }
        break;
    case Tool::Parallel:
        if (both(isLine)) {
            apply(ConstraintKind::Parallel, {first.id, hit.id}, tr("Parallel"));
            return;
        }
        break;
    case Tool::Perpendicular:
        if (both(isLine)) {
            apply(ConstraintKind::Perpendicular, {first.id, hit.id}, tr("Perpendicular"));
            return;
        }
        break;
    case Tool::Tangent:
        if (isRoundHit(hit) && first.kind == Hit::Kind::Curve) {
            apply(ConstraintKind::Tangent, {first.id, hit.id}, tr("Tangent"));
            return;
        }
        if (isRoundHit(first) && isLine(hit)) {
            apply(ConstraintKind::Tangent, {hit.id, first.id}, tr("Tangent"));
            return;
        }
        break;
    case Tool::Equal:
        if (both(isLine) || both(isRoundHit)) {
            apply(ConstraintKind::Equal, {first.id, hit.id}, tr("Equal"));
            return;
        }
        break;
    case Tool::Midpoint:
        if (isPointHit(first) && isLine(hit)) {
            apply(ConstraintKind::Midpoint, {first.id, hit.id}, tr("Midpoint"));
            return;
        }
        if (isLine(first) && isPointHit(hit)) {
            apply(ConstraintKind::Midpoint, {hit.id, first.id}, tr("Midpoint"));
            return;
        }
        break;
    case Tool::Select:
    case Tool::Line:
    case Tool::Rectangle:
    case Tool::CenterRectangle:
    case Tool::Circle:
    case Tool::Arc:
    case Tool::ArcThrough:
    case Tool::Polygon:
    case Tool::Point:
    case Tool::Trim:
    case Tool::Fillet:
    case Tool::Mirror:
    case Tool::Dimension:
    case Tool::Symmetric:
    case Tool::Fix:
        return;
    }
    emit message(tr("Those can't take this constraint. %1").arg(hint()));
}

} // namespace kalve137::app
