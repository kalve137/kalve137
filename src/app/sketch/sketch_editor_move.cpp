// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/sketch/sketch_editor_detail.h"
#include "app/sketch/sketch_inputs.h"
#include "app/theme/theme.h"

#include "sketch/profiles.h"
#include "sketch/solver.h"
#include "viewer/box_band.h"
#include "viewer/grid.h"
#include "viewer/occt_view_widget.h"

#include <QPointF>

#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace kalve137::app {

using namespace sketch_editor_detail;

std::vector<Id> SketchEditor::movablePoints() const
{
    const sketch::Sketch& s = current();
    std::set<Id> points;
    for (const Id id : m_selection) {
        if (s.isPoint(id)) {
            points.insert(id);
        } else if (const sketch::Curve* curve = s.curve(id); curve != nullptr && !curve->fixed) {
            points.insert(curve->points.begin(), curve->points.end());
        }
    }
    std::erase_if(points, [&s](Id id) { return id == sketch::kOriginId || s.point(id)->fixed; });
    return {points.begin(), points.end()};
}

void SketchEditor::startGroupDrag(QPointF pos, Vec2 at, std::vector<Id> points)
{
    if (points.empty()) {
        return;
    }
    m_dragGrab = points.size() == 1 ? current().point(points.front())->pos : at;
    m_dragPoints = std::move(points);
    m_dragPress = pos;
    m_draggingGroup = false;
    m_dragStart = current();
    m_dragPreview.reset();
}

void SketchEditor::dragGroup(QPointF pos, Vec2 at)
{
    if (!m_draggingGroup && !viewer::isBoxDrag(m_dragPress, pos)) {
        return;
    }
    m_draggingGroup = true;
    Vec2 delta = at - m_dragGrab;
    if (m_moveIncrement > 0.0) {
        delta = {viewer::roundToIncrement(delta.x, m_moveIncrement),
                 viewer::roundToIncrement(delta.y, m_moveIncrement)};
    }
    moveBy(delta);
}

bool SketchEditor::moveBy(Vec2 delta)
{
    if (!m_dragStart || m_dragPoints.empty()) {
        return false;
    }
    std::vector<sketch::DragTarget> targets;
    for (const Id id : m_dragPoints) {
        if (const sketch::Point* point = m_dragStart->point(id)) {
            targets.push_back({id, point->pos + delta});
        }
    }
    sketch::Sketch candidate = m_dragPreview ? *m_dragPreview : *m_dragStart;
    const sketch::SolveResult result = sketch::solve(candidate, targets);
    if (!result.solved || !result.conflicting.empty()) {
        return false;
    }
    m_dragPreview = std::move(candidate);
    redraw();
    return true;
}

void SketchEditor::startTypedMove()
{
    if (!isActive()) {
        return;
    }
    closeValueEditor();
    std::vector<Id> points = movablePoints();
    if (points.empty()) {
        emit message(tr("Select sketch geometry to move first"));
        return;
    }
    dropDrag();
    resetProgress();
    m_dragPoints = std::move(points);
    m_dragStart = current();
    m_dragPreview.reset();
    m_typedMove = true;
    m_inputs->open({SketchInputs::Kind::Length, SketchInputs::Kind::Length});
    m_inputs->setPlaceholder(0, tr("X offset"));
    m_inputs->setPlaceholder(1, tr("Y offset"));
    placeMoveInputs(current());
    redraw();
    emit stateChanged();
}

void SketchEditor::placeMoveInputs(const sketch::Sketch& s)
{
    const gp_Ax3 ax = plane();
    double right = -std::numeric_limits<double>::infinity();
    double top = std::numeric_limits<double>::infinity();
    double bottom = -std::numeric_limits<double>::infinity();
    const auto extend = [&](Vec2 p) {
        const QPointF screen = m_view->toScreen(sketch::toWorld(ax, p));
        right = std::max(right, screen.x());
        top = std::min(top, screen.y());
        bottom = std::max(bottom, screen.y());
    };
    for (const Id id : m_dragPoints) {
        extend(s.point(id)->pos);
    }
    for (const Id id : m_selection) {
        if (const sketch::Curve* curve = s.curve(id); curve != nullptr && isRound(curve->kind)) {
            const Vec2 c = s.point(curve->points.front())->pos;
            for (const Vec2 offset : {Vec2{curve->radius, 0.0}, Vec2{-curve->radius, 0.0}, Vec2{0.0, curve->radius},
                                      Vec2{0.0, -curve->radius}}) {
                extend(c + offset);
            }
        }
    }
    const double half = (m_inputs->boxSize(0).height() + theme::kSpaceS) / 2.0;
    const QPointF side(right + kInputOffsetPixels, (top + bottom) / 2.0);
    m_inputs->place(0, side - QPointF(0.0, half), {1.0, 0.0});
    m_inputs->place(1, side + QPointF(0.0, half), {1.0, 0.0});
}

void SketchEditor::previewTypedMove()
{
    const Vec2 delta{m_inputs->held(0).value_or(0.0), m_inputs->held(1).value_or(0.0)};
    if (!moveBy(delta)) {
        emit message(tr("The constraints don't allow that offset"));
    }
    placeMoveInputs(m_dragPreview ? *m_dragPreview : current());
}

void SketchEditor::acceptTypedMove()
{
    const bool typed = anyHeld();
    if (typed) {
        previewTypedMove();
    }
    std::optional<sketch::Sketch> moved = typed ? std::move(m_dragPreview) : std::nullopt;
    resetProgress();
    if (moved) {
        commit(tr("Move"), std::move(*moved));
    } else {
        redraw();
    }
    emit stateChanged();
}

} // namespace kalve137::app
