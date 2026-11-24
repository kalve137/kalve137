// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/sketch/sketch_editor_detail.h"
#include "app/sketch/sketch_inputs.h"

#include "sketch/build.h"
#include "sketch/offset.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace kalve137::app {

using namespace sketch_editor_detail;

namespace {

constexpr int kMaxPolygonSides = 64;

}

Vec2 SketchEditor::draftCenterCorner(Vec2 cursor) const
{
    if (!m_first) {
        return cursor;
    }
    const Vec2 c = m_first->pos;
    Vec2 corner = cursor;
    if (const std::optional<double> width = heldPositive(0)) {
        corner.x = c.x + (cursor.x >= c.x ? *width : -*width) / 2.0;
    }
    if (const std::optional<double> height = heldPositive(1)) {
        corner.y = c.y + (cursor.y >= c.y ? *height : -*height) / 2.0;
    }
    return corner;
}

Vec2 SketchEditor::draftPolygonCorner(Vec2 cursor) const
{
    const std::optional<double> diameter = heldPositive(0);
    if (!m_first || !diameter) {
        return cursor;
    }
    const Vec2 c = m_first->pos;
    const Vec2 dir = sketch::distance(c, cursor) > 1e-9 ? unit(cursor - c) : Vec2{1.0, 0.0};
    return c + dir * (*diameter / 2.0);
}

void SketchEditor::clickMirror(Vec2 pos)
{
    const Hit hit = curveAt(pos);
    if (hit.kind != Hit::Kind::Curve) {
        return;
    }
    if (m_mirrorAxis != 0 && current().curve(m_mirrorAxis) == nullptr) {
        m_mirrorAxis = 0;
    }
    std::vector<Id> curves;
    if (m_mirrorAxis == 0) {
        if (current().curve(hit.id)->kind != CurveKind::Line) {
            emit message(tr("The mirror line has to be a straight line"));
            return;
        }
        for (Id id : m_selection) {
            if (current().isCurve(id) && id != hit.id) {
                curves.push_back(id);
            }
        }
        m_mirrorAxis = hit.id;
        m_selection = {hit.id};
        if (curves.empty()) {
            redraw();
            return;
        }
    } else {
        curves.push_back(hit.id);
    }
    sketch::Sketch candidate = current();
    if (!sketch::addMirror(candidate, curves, m_mirrorAxis)) {
        emit message(tr("That lies on the mirror line, so its mirror image is itself"));
        redraw();
        return;
    }
    commit(tr("Mirror"), std::move(candidate));
}

void SketchEditor::clickOffset(QPointF screen, Vec2 pos)
{
    const Hit hit = curveAt(pos);
    if (hit.kind != Hit::Kind::Curve) {
        emit message(hint());
        return;
    }
    const sketch::Sketch& s = current();
    std::vector<Id> curves;
    if (m_selection.contains(hit.id)) {
        std::ranges::copy_if(m_selection, std::back_inserter(curves), [&](Id id) { return s.isCurve(id); });
    }
    if (curves.empty() || !sketch::orderChain(s, curves)) {
        curves = sketch::chainThrough(s, hit.id);
    }
    if (!sketch::orderChain(s, curves)) {
        emit message(tr("Pick curves joined end to end, without branches"));
        return;
    }
    m_offsetSide = 1;
    m_selection = {curves.begin(), curves.end()};
    redraw();
    const auto offsetBy = [this](double typed) { return typed * m_offsetSide; };
    promptValue(
        screen, m_offsetDistance, core::Quantity::Length, 0,
        [this, curves, offsetBy](double typed, const std::string&) {
            sketch::Sketch candidate = current();
            if (!sketch::addOffset(candidate, curves, offsetBy(typed))) {
                emit message(tr("A %1 mm offset doesn't fit these curves").arg(formatValue(std::abs(typed))));
                return;
            }
            m_offsetDistance = std::abs(typed);
            m_selection.clear();
            commit(tr("Offset"), std::move(candidate));
        },
        [this, curves, offsetBy](double typed) {
            sketch::Sketch candidate = current();
            sketch::addOffset(candidate, curves, offsetBy(typed), false);
            return candidate;
        });
    m_offsetChain = curves;
    emit stateChanged();
}

int SketchEditor::polygonSides() const
{
    if (const std::optional<double> held = m_inputs->held(1)) {
        return std::clamp(static_cast<int>(std::lround(*held)), 3, kMaxPolygonSides);
    }
    return m_polygonSides;
}

void SketchEditor::clickCenterRectangle(Snap snap)
{
    if (!m_first) {
        m_first = snap;
        openInputs();
        return;
    }
    const Vec2 corner = draftCenterCorner(snap.pos);
    const Vec2 c = m_first->pos;
    if (std::abs(corner.x - c.x) < tolerance() * 0.25 || std::abs(corner.y - c.y) < tolerance() * 0.25) {
        return;
    }
    const std::optional<double> width = heldPositive(0);
    const std::optional<double> height = heldPositive(1);
    const auto build = [&](bool dimensions) {
        sketch::Sketch candidate = current();
        const sketch::RectangleSides sides =
            sketch::addCenterRectangle(candidate, placePoint(candidate, *m_first), corner);
        if (dimensions && width) {
            candidate.addConstraint(ConstraintKind::Distance, {sides.bottom}, *width);
        }
        if (dimensions && height) {
            candidate.addConstraint(ConstraintKind::Distance, {sides.right}, *height);
        }
        return candidate;
    };
    if (commit(tr("Center Rectangle"), withDimensionsIfSolvable(build, m_solve.redundant.size()))) {
        resetProgress();
    }
}

void SketchEditor::clickPolygon(Snap snap)
{
    if (!m_first) {
        m_first = snap;
        openInputs();
        return;
    }
    const Vec2 corner = draftPolygonCorner(snap.pos);
    if (sketch::distance(m_first->pos, corner) < tolerance() * 0.5) {
        return;
    }
    const int sides = polygonSides();
    const std::optional<double> diameter = heldPositive(0);
    const auto build = [&](bool dimensions) {
        sketch::Sketch candidate = current();
        const sketch::Polygon polygon = sketch::addPolygon(candidate, placePoint(candidate, *m_first), corner, sides);
        if (dimensions && diameter) {
            candidate.addConstraint(ConstraintKind::Diameter, {polygon.circle}, *diameter);
        }
        return candidate;
    };
    if (commit(tr("Polygon"), withDimensionsIfSolvable(build, m_solve.redundant.size()))) {
        m_polygonSides = sides;
        resetProgress();
    }
}

Vec2 SketchEditor::draftSlotEnd(Vec2 cursor) const
{
    const std::optional<double> length = heldPositive(0);
    if (!m_first || !length) {
        return cursor;
    }
    const Vec2 a = m_first->pos;
    const Vec2 dir = sketch::distance(a, cursor) > 1e-9 ? unit(cursor - a) : Vec2{1.0, 0.0};
    return a + dir * (m_tool == Tool::CenterSlot ? *length / 2.0 : *length);
}

double SketchEditor::draftSlotWidth(Vec2 cursor) const
{
    if (const std::optional<double> width = heldPositive(1)) {
        return *width;
    }
    if (!m_first || !m_second) {
        return 0.0;
    }
    const Vec2 a = m_first->pos;
    const Vec2 dir = unit(m_second->pos - a);
    return 2.0 * std::abs((cursor.x - a.x) * dir.y - (cursor.y - a.y) * dir.x);
}

std::optional<std::pair<Vec2, Vec2>> SketchEditor::slotCenters(Vec2 end, double width) const
{
    if (!m_first) {
        return std::nullopt;
    }
    const Vec2 a = m_first->pos;
    std::pair<Vec2, Vec2> centers{a, end};
    if (m_tool == Tool::CenterSlot) {
        centers.first = a * 2.0 - end;
    } else if (m_tool == Tool::OverallSlot) {
        const double length = sketch::distance(a, end);
        if (length <= width) {
            return std::nullopt;
        }
        const Vec2 inset = (end - a) * (width / 2.0 / length);
        centers = {a + inset, end - inset};
    }
    if (sketch::distance(centers.first, centers.second) < 1e-9) {
        return std::nullopt;
    }
    return centers;
}

void SketchEditor::clickSlot(Snap snap)
{
    if (!m_first) {
        m_first = snap;
        openInputs();
        return;
    }
    if (!m_second) {
        if (heldPositive(0)) {
            snap = Snap{draftSlotEnd(snap.pos)};
        }
        if (sketch::distance(m_first->pos, snap.pos) < tolerance() * 0.5) {
            return;
        }
        m_second = snap;
        if (!heldPositive(1)) {
            return;
        }
    }
    const double width = draftSlotWidth(snap.pos);
    if (width < tolerance() * 0.5) {
        return;
    }
    const std::optional<std::pair<Vec2, Vec2>> centers = slotCenters(m_second->pos, width);
    if (!centers) {
        emit message(tr("The slot has to be longer than it is wide"));
        return;
    }
    const std::optional<double> length = heldPositive(0);
    const double centerLength = m_tool == Tool::OverallSlot && length ? *length - width : length.value_or(0.0);
    const auto build = [&](bool dimensions, bool autoAxis) {
        sketch::Sketch candidate = current();
        const Id start = m_tool == Tool::Slot ? placePoint(candidate, *m_first) : candidate.addPoint(centers->first);
        const Id end =
            m_tool == Tool::OverallSlot ? candidate.addPoint(centers->second) : placePoint(candidate, *m_second);
        const sketch::Slot slot = sketch::addSlot(candidate, start, end, width);
        if (const std::optional<ConstraintKind> axis = autoAxisOf(centers->second - centers->first); axis && autoAxis) {
            candidate.addConstraint(*axis, {slot.centerLine});
        }
        if (m_tool == Tool::CenterSlot) {
            candidate.addConstraint(ConstraintKind::Midpoint, {placePoint(candidate, *m_first), slot.centerLine});
        }
        if (dimensions) {
            candidate.addConstraint(ConstraintKind::Distance,
                                    {candidate.curve(slot.sides[0])->points[0], slot.sides[1]}, width);
            if (centerLength > 0.0) {
                candidate.addConstraint(ConstraintKind::Distance, {slot.centerLine}, centerLength);
            }
        }
        return candidate;
    };
    const std::size_t redundant = m_solve.redundant.size();
    sketch::Sketch candidate =
        withDimensionsIfSolvable([&](bool dimensions) { return build(dimensions, true); }, redundant);
    if (!solvesWithin(candidate, redundant)) {
        candidate = withDimensionsIfSolvable([&](bool dimensions) { return build(dimensions, false); }, redundant);
    }
    if (commit(tr("Slot"), std::move(candidate))) {
        resetProgress();
    }
}

void SketchEditor::clickArcThrough(const Snap& snap)
{
    if (!m_first) {
        m_first = snap;
        return;
    }
    if (!m_second) {
        if (sketch::distance(m_first->pos, snap.pos) >= tolerance() * 0.5) {
            m_second = snap;
        }
        return;
    }
    sketch::Sketch candidate = current();
    const Id start = placePoint(candidate, *m_first);
    const Id end = placePoint(candidate, *m_second);
    if (!sketch::addArcThrough(candidate, start, end, snap.pos)) {
        emit message(tr("The three points line up, so no arc passes through them"));
        return;
    }
    if (commit(tr("Arc"), std::move(candidate))) {
        resetProgress();
    }
}

void SketchEditor::clickPoint(const Snap& snap)
{
    if (snap.point != 0) {
        emit message(tr("There is already a point there"));
        return;
    }
    sketch::Sketch candidate = current();
    placePoint(candidate, snap);
    commit(tr("Point"), std::move(candidate));
}

} // namespace kalve137::app
