// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/sketch/sketch_editor_detail.h"
#include "app/sketch/sketch_inputs.h"

#include "sketch/build.h"

#include <algorithm>
#include <cmath>
#include <optional>
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
    const Vec2 dir = sketch::distance(c, cursor) > 1e-9 ? normalized(cursor - c) : Vec2{1.0, 0.0};
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
