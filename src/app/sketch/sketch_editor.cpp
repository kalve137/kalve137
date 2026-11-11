// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/feature/value_edit.h"
#include "app/session/design_session.h"
#include "app/sketch/sketch_editor_detail.h"
#include "app/sketch/sketch_inputs.h"

#include "geometry/shapes.h"
#include "log/log.h"
#include "sketch/draft.h"
#include "sketch/profiles.h"
#include "sketch/select.h"
#include "viewer/box_band.h"
#include "viewer/grid.h"
#include "viewer/occt_view_widget.h"

#include <PrsDim_Dimension.hxx>
#include <gp_Pln.hxx>

#include <QLineEdit>
#include <QScopedValueRollback>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace kalve137::app {

using namespace sketch_editor_detail;

SketchEditor::SketchEditor(DesignSession& session, viewer::OcctViewWidget* view, QObject* parent)
    : QObject(parent), m_session(session), m_view(view)
{
    m_valueEditor = new QLineEdit(view);
    m_valueEditor->setObjectName("dimensionEditor");
    m_valueEditor->setFixedWidth(90);
    m_valueEditor->hide();
    connect(m_valueEditor, &QLineEdit::returnPressed, this, [this] {
        const std::optional<double> parsed = parseValue(m_valueEditor->text(), m_valueQuantity);
        if (!parsed) {
            emit message(tr("Type a number or an expression, such as 30/2 or 1in"));
            return;
        }
        const double value = *parsed;
        std::function<void(double)> apply = std::move(m_valueApply);
        closeValueEditor();
        if (apply) {
            apply(value);
        }
    });
    connect(m_valueEditor, &QLineEdit::textEdited, this, &SketchEditor::previewValue);
    connect(m_valueEditor, &QLineEdit::editingFinished, this, [this] {
        if (!m_valueEditor->hasFocus()) {
            closeValueEditor();
        }
    });

    m_inputs = new SketchInputs(view, this);
    connect(m_inputs, &SketchInputs::edited, this, &SketchEditor::redrawPreview);
    connect(m_inputs, &SketchInputs::accepted, this, &SketchEditor::onInputsAccepted);

    connect(view, &viewer::OcctViewWidget::capturedPress, this, &SketchEditor::onPress);
    connect(view, &viewer::OcctViewWidget::capturedRelease, this, &SketchEditor::onRelease);
    connect(view, &viewer::OcctViewWidget::capturedDoubleClick, this, &SketchEditor::onDoubleClick);
    connect(view, &viewer::OcctViewWidget::pointerMoved, this, &SketchEditor::onMove);
    connect(&session, &DesignSession::changed, this, &SketchEditor::onSessionChanged);

    m_rescale = new QTimer(this);
    m_rescale->setSingleShot(true);
    m_rescale->setInterval(kRescaleDelayMs);
    connect(m_rescale, &QTimer::timeout, this, [this] {
        if (isActive()) {
            redraw();
        }
    });
    connect(view, &viewer::OcctViewWidget::rendered, this, [this] {
        if (isActive() && m_drawnScale > 0.0 &&
            std::abs(m_view->worldPerPixel() / m_drawnScale - 1.0) > kRescaleTolerance) {
            m_rescale->start();
        }
    });
}

SketchEditor::~SketchEditor() = default;

void SketchEditor::begin(core::FeatureId sketch)
{
    end();
    m_sketchId = sketch;
    if (feature() == nullptr) {
        m_sketchId = 0;
        return;
    }
    m_tool = Tool::Select;
    m_view->setLeftButtonCaptured(true);
    redraw();
    emit toolChanged();
}

void SketchEditor::end()
{
    if (!isActive()) {
        return;
    }
    closeValueEditor();
    resetProgress();
    m_selection.clear();
    m_hover = 0;
    dropDrag();
    clearObjects(m_objects);
    clearObjects(m_preview);
    m_sketchId = 0;
    m_view->unsetCursor();
    m_view->setLeftButtonCaptured(false);
    m_view->redraw();
    emit toolChanged();
}

void SketchEditor::setTool(Tool tool)
{
    if (!isActive()) {
        return;
    }
    closeValueEditor();
    resetProgress();
    m_tool = tool;
    if (drawsGeometry(tool)) {
        m_view->setCursor(Qt::CrossCursor);
    } else {
        m_view->unsetCursor();
    }
    if (tool != Tool::Select && tool != Tool::Mirror) {
        m_selection.clear();
        redraw();
    }
    redrawPreview();
    emit toolChanged();
}

void SketchEditor::cancel()
{
    if (m_valueEditor->isVisible()) {
        closeValueEditor();
        return;
    }
    if (m_dragPoint != 0) {
        dropDrag();
        redraw();
        return;
    }
    if (m_first || m_second || !m_picks.empty() || m_pending || m_mirrorAxis != 0) {
        resetProgress();
        redraw();
        redrawPreview();
        emit stateChanged();
        return;
    }
    if (m_tool != Tool::Select) {
        setTool(Tool::Select);
        return;
    }
    if (!m_selection.empty()) {
        m_selection.clear();
        redraw();
    }
}

void SketchEditor::deleteSelection()
{
    if (!isActive() || m_selection.empty()) {
        return;
    }
    sketch::Sketch candidate = current();
    for (Id id : m_selection) {
        if (id == sketch::kOriginId) {
            continue;
        }
        if (candidate.isPoint(id) || candidate.isCurve(id) || candidate.constraint(id) != nullptr) {
            candidate.remove(id);
        }
    }
    m_selection.clear();
    commit(tr("Delete"), std::move(candidate));
}

void SketchEditor::toggleConstruction()
{
    if (!isActive()) {
        return;
    }
    sketch::Sketch candidate = current();
    bool any = false;
    for (Id id : m_selection) {
        if (sketch::Curve* curve = candidate.curve(id)) {
            curve->construction = !curve->construction;
            any = true;
        }
    }
    if (!any) {
        emit message(tr("Select curves to toggle construction"));
        return;
    }
    commit(tr("Toggle Construction"), std::move(candidate));
}

QString SketchEditor::hint() const
{
    switch (m_tool) {
    case Tool::Select:
        return tr("Click to select, drag points to move, double-click a dimension to edit it");
    case Tool::Line:
        return m_first ? tr("Type a length, Tab to the next value, Enter to finish, Esc to stop")
                       : tr("Click the start point");
    case Tool::Rectangle:
        return m_first ? tr("Click the opposite corner, or type the width, Tab, the height and Enter")
                       : tr("Click the first corner");
    case Tool::CenterRectangle:
        return m_first ? tr("Click a corner, or type the width, Tab, the height and Enter") : tr("Click the center");
    case Tool::Polygon:
        return m_first ? tr("Click a corner, or type the diameter, Tab, the number of sides and Enter")
                       : tr("Click the center");
    case Tool::ArcThrough:
        if (!m_first) {
            return tr("Click the start point");
        }
        return m_second ? tr("Click a point the arc passes through") : tr("Click the end point");
    case Tool::Point:
        return tr("Click to place a point");
    case Tool::Circle:
        return m_first ? tr("Click to set the size, or type the diameter and Enter") : tr("Click the center");
    case Tool::Arc:
        if (!m_first) {
            return tr("Click the arc center");
        }
        return m_second ? tr("Click the end point") : tr("Click the start point");
    case Tool::Trim:
        return tr("Click the part of a curve to remove, up to where other curves cross it");
    case Tool::Fillet:
        return tr("Click a corner where two lines meet, then type the radius");
    case Tool::Mirror:
        if (m_mirrorAxis != 0) {
            return tr("Click curves to mirror across the line, Esc to pick another line");
        }
        return m_selection.empty() ? tr("Click the mirror line")
                                   : tr("Click the mirror line to mirror the selected curves");
    case Tool::Dimension:
        return m_pending ? tr("Click to place the dimension, Esc to cancel")
                         : tr("Click a line, circle, arc or two points, then click to place");
    case Tool::Coincident:
        return tr("Click a point, then a point or curve");
    case Tool::Horizontal:
    case Tool::Vertical:
        return tr("Click a line, or two points");
    case Tool::Parallel:
    case Tool::Perpendicular:
        return tr("Click two lines");
    case Tool::Tangent:
        return tr("Click a curve and a circle or arc");
    case Tool::Equal:
        return tr("Click two lines or two circles and arcs");
    case Tool::Midpoint:
        return tr("Click a point and a line");
    case Tool::Symmetric:
        return m_picks.size() < 2 ? tr("Click two points, then the line they mirror across")
                                  : tr("Click the line the two points mirror across");
    case Tool::Fix:
        return tr("Click a point or curve to fix or free it");
    }
    return {};
}

QString SketchEditor::solveSummary() const
{
    if (!isActive()) {
        return {};
    }
    if (!m_solve.solved || !m_solve.conflicting.empty()) {
        return tr("Over-constrained");
    }
    if (m_solve.dof == 0) {
        const core::SketchFeature* sketch = feature();
        if (sketch == nullptr || sketch->sketch.curves().empty()) {
            return {};
        }
        return tr("Fully constrained");
    }
    if (m_solve.dof < 0) {
        return {};
    }
    return tr("%n degrees of freedom", nullptr, m_solve.dof);
}

QString SketchEditor::selectionSummary() const
{
    if (!isActive() || m_selection.empty()) {
        return {};
    }
    const sketch::Sketch& s = current();
    const gp_Ax3 ax = plane();
    int curves = 0;
    int points = 0;
    int constraints = 0;
    double total = 0.0;
    for (Id id : m_selection) {
        if (s.isCurve(id)) {
            ++curves;
            if (const std::optional<TopoDS_Edge> edge = sketch::makeEdge(s, id, ax)) {
                total += geometry::length(*edge);
            }
        } else if (s.point(id) != nullptr) {
            ++points;
        } else if (s.constraint(id) != nullptr) {
            ++constraints;
        }
    }
    const QString curveText = curves == 1 ? tr("1 Sketch Curve") : tr("%1 Sketch Curves").arg(curves);
    if (points == 0 && constraints == 0) {
        return tr("%1 | Length : %2 mm").arg(curveText).arg(total, 0, 'f', 2);
    }
    QStringList parts;
    if (curves > 0) {
        parts << curveText;
    }
    if (points > 0) {
        parts << (points == 1 ? tr("1 Point") : tr("%1 Points").arg(points));
    }
    if (constraints > 0) {
        parts << (constraints == 1 ? tr("1 Constraint") : tr("%1 Constraints").arg(constraints));
    }
    return parts.join(QStringLiteral(" | "));
}

bool SketchEditor::drawsGeometry(Tool tool)
{
    switch (tool) {
    case Tool::Line:
    case Tool::Rectangle:
    case Tool::CenterRectangle:
    case Tool::Circle:
    case Tool::Arc:
    case Tool::ArcThrough:
    case Tool::Polygon:
    case Tool::Point:
        return true;
    case Tool::Select:
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
        return false;
    }
    return false;
}

void SketchEditor::setDisplay(const Display& display)
{
    if (display == m_display) {
        return;
    }
    m_display = display;
    redraw();
}

const core::SketchFeature* SketchEditor::feature() const
{
    const core::Feature* feature = m_session.document().find(m_sketchId);
    return feature != nullptr ? std::get_if<core::SketchFeature>(&feature->data) : nullptr;
}

const sketch::Sketch& SketchEditor::current() const
{
    return feature()->sketch;
}

const sketch::Sketch& SketchEditor::shownSketch() const
{
    return m_dragPreview ? *m_dragPreview : current();
}

gp_Ax3 SketchEditor::plane() const
{
    return m_session.sketchPlane(m_sketchId);
}

void SketchEditor::onPress(QPointF pos, Qt::KeyboardModifiers modifiers)
{
    if (!isActive() || feature() == nullptr) {
        return;
    }
    closeValueEditor();
    const std::optional<Vec2> p = toSketch(pos);
    if (!p) {
        return;
    }
    switch (m_tool) {
    case Tool::Select: {
        const Hit hit = hitAt(pos);
        clickSelect(hit, modifiers);
        if (hit.kind == Hit::Kind::None) {
            m_boxStart = pos;
        }
        break;
    }
    case Tool::Line:
        clickLine(snapAt(*p));
        break;
    case Tool::Rectangle:
        clickRectangle(snapAt(*p));
        break;
    case Tool::Circle:
        clickCircle(snapAt(*p));
        break;
    case Tool::Arc:
        clickArc(snapAt(*p));
        break;
    case Tool::CenterRectangle:
        clickCenterRectangle(snapAt(*p));
        break;
    case Tool::Polygon:
        clickPolygon(snapAt(*p));
        break;
    case Tool::ArcThrough:
        clickArcThrough(snapAt(*p));
        break;
    case Tool::Point:
        clickPoint(snapAt(*p));
        break;
    case Tool::Trim:
        clickTrim(*p);
        break;
    case Tool::Fillet:
        clickFillet(pos, *p);
        break;
    case Tool::Mirror:
        clickMirror(*p);
        break;
    case Tool::Dimension:
        clickDimension(pos, hitAt(pos));
        break;
    case Tool::Coincident:
    case Tool::Horizontal:
    case Tool::Vertical:
    case Tool::Parallel:
    case Tool::Perpendicular:
    case Tool::Tangent:
    case Tool::Equal:
    case Tool::Midpoint:
    case Tool::Symmetric:
    case Tool::Fix: {
        const Hit hit = geometryAt(*p);
        if (hit.kind != Hit::Kind::None) {
            clickConstraint(hit);
        }
        break;
    }
    }
    redrawPreview();
    emit stateChanged();
}

void SketchEditor::onRelease(QPointF pos)
{
    if (m_boxStart) {
        const QPointF from = *m_boxStart;
        const bool boxing = m_boxing;
        dropBox();
        if (boxing) {
            selectInBox(from, pos);
        }
        return;
    }
    if (m_dragPoint == 0) {
        return;
    }
    m_dragPoint = 0;
    std::optional<sketch::Sketch> moved = std::move(m_dragPreview);
    m_dragPreview.reset();
    m_dragStart.reset();
    if (moved) {
        commit(tr("Move"), std::move(*moved));
    } else {
        redraw();
    }
}

void SketchEditor::onMove(QPointF pos, Qt::MouseButtons buttons)
{
    if (!isActive() || feature() == nullptr) {
        return;
    }
    if (m_boxStart && (buttons & Qt::LeftButton) && (m_boxing || viewer::isBoxDrag(*m_boxStart, pos))) {
        m_boxing = true;
        m_view->showBand(*m_boxStart, pos);
        return;
    }
    const std::optional<Vec2> p = toSketch(pos);
    if (!p) {
        return;
    }
    m_cursor = *p;
    m_hasCursor = true;

    if (m_dragPoint != 0 && (buttons & Qt::LeftButton) && m_dragStart) {
        sketch::Sketch candidate = *m_dragStart;
        sketch::Point* point = candidate.point(m_dragPoint);
        if (m_moveIncrement > 0.0) {
            const Vec2 from = point->pos;
            point->pos = {from.x + viewer::roundToIncrement(p->x - from.x, m_moveIncrement),
                          from.y + viewer::roundToIncrement(p->y - from.y, m_moveIncrement)};
        } else {
            point->pos = *p;
        }
        point->fixed = true;
        const sketch::SolveResult result = sketch::solve(candidate);
        if (result.solved && result.conflicting.empty()) {
            candidate.point(m_dragPoint)->fixed = false;
            m_dragPreview = std::move(candidate);
            redraw();
        }
        return;
    }

    switch (m_tool) {
    case Tool::Line:
    case Tool::Rectangle:
    case Tool::CenterRectangle:
    case Tool::Circle:
    case Tool::Arc:
    case Tool::ArcThrough:
    case Tool::Polygon:
    case Tool::Point:
        m_cursorSnap = snapAt(*p);
        break;
    case Tool::Select:
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
        m_cursorSnap = Snap{*p};
        break;
    }

    if (m_tool == Tool::Arc && m_first && m_second) {
        const double angle = angleOf(m_cursorSnap.pos - m_first->pos);
        double delta = angle - m_arcLastAngle;
        while (delta > std::numbers::pi) {
            delta -= kTwoPi;
        }
        while (delta < -std::numbers::pi) {
            delta += kTwoPi;
        }
        m_arcSweep = std::clamp(m_arcSweep + delta, -kTwoPi, kTwoPi);
        m_arcLastAngle = angle;
    }

    const Hit hover = geometryAt(*p);
    const bool ownPreview = m_tool == Tool::Trim || m_tool == Tool::Fillet;
    const Id hoverId = hover.kind == Hit::Kind::None || ownPreview ? 0 : hover.id;
    if (hoverId != m_hover || m_pending) {
        m_hover = hoverId;
        redraw();
    }
    redrawPreview();
}

void SketchEditor::onDoubleClick(QPointF pos)
{
    if (!isActive()) {
        return;
    }
    if (m_tool == Tool::Line && m_first) {
        resetProgress();
        redrawPreview();
        emit stateChanged();
        return;
    }
    m_view->detectAt(pos);
    if (const std::optional<Id> constraint = detectedConstraint()) {
        editDimension(*constraint, pos);
    }
}

void SketchEditor::onSessionChanged()
{
    if (!isActive()) {
        return;
    }
    if (feature() == nullptr) {
        end();
        emit finished();
        return;
    }
    if (!m_committing) {
        closeValueEditor();
        resetProgress();
        dropDrag();
        redrawPreview();
    }
    std::erase_if(m_selection, [this](Id id) {
        const sketch::Sketch& s = current();
        return !s.isPoint(id) && !s.isCurve(id) && s.constraint(id) == nullptr;
    });
    redraw();
    emit stateChanged();
}

void SketchEditor::clickSelect(const Hit& hit, Qt::KeyboardModifiers modifiers)
{
    const bool toggle = (modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) != 0;
    if (hit.kind == Hit::Kind::None) {
        if (!toggle) {
            m_selection.clear();
        }
        redraw();
        return;
    }
    if (toggle) {
        if (!m_selection.erase(hit.id)) {
            m_selection.insert(hit.id);
        }
    } else if (!m_selection.contains(hit.id)) {
        m_selection = {hit.id};
    }
    if (hit.kind == Hit::Kind::Point) {
        const sketch::Point* point = current().point(hit.id);
        if (point != nullptr && !point->fixed) {
            m_dragPoint = hit.id;
            m_dragStart = current();
            m_dragPreview.reset();
        }
    }
    redraw();
}

bool SketchEditor::commit(const QString& label, sketch::Sketch candidate)
{
    const sketch::SolveResult result = sketch::solve(candidate);
    if (!result.solved || !result.conflicting.empty()) {
        emit message(tr("That would over-constrain the sketch"));
        redraw();
        return false;
    }
    if (result.redundant.size() > m_solve.redundant.size()) {
        emit message(tr("That constraint is already implied by others"));
        redraw();
        return false;
    }
    const core::FeatureId id = m_sketchId;
    const QScopedValueRollback committing(m_committing, true);
    m_session.edit(label, [id, &candidate](core::Document& document) {
        if (core::Feature* f = document.find(id)) {
            std::get<core::SketchFeature>(f->data).sketch = std::move(candidate);
        }
    });
    return true;
}

void SketchEditor::addDimension(sketch::Constraint constraint, QPointF pos)
{
    const ConstraintKind kind = constraint.kind;
    const double value = constraint.value;
    const auto build = [this, constraint = std::move(constraint)](double typed) {
        sketch::Sketch candidate = current();
        const Id id = candidate.addConstraint(constraint.kind, constraint.refs, typed);
        candidate.constraint(id)->labelOffset = constraint.labelOffset;
        return candidate;
    };
    promptValue(
        pos, value, quantityOf(kind), true,
        [this, kind, build](double typed) {
            if (typed <= 0.0 && kind != ConstraintKind::Angle) {
                emit message(tr("Dimensions must be greater than zero"));
                return;
            }
            commit(tr("Dimension"), build(typed));
        },
        build);
}

void SketchEditor::editDimension(Id constraint, QPointF pos)
{
    const sketch::Constraint* c = current().constraint(constraint);
    if (c == nullptr || !sketch::isDimension(c->kind)) {
        return;
    }
    const ConstraintKind kind = c->kind;
    bool onLabel = false;
    for (const auto& [object, id] : m_constraintObjects) {
        if (const auto* dimension = dynamic_cast<const PrsDim_Dimension*>(object);
            dimension != nullptr && id == constraint) {
            pos = m_view->toScreen(dimension->GetTextPosition());
            onLabel = true;
            break;
        }
    }
    const auto build = [this, constraint](double typed) {
        sketch::Sketch candidate = current();
        if (sketch::Constraint* edited = candidate.constraint(constraint)) {
            edited->value = typed;
        }
        return candidate;
    };
    promptValue(
        pos, c->value, quantityOf(kind), onLabel,
        [this, kind, build](double typed) {
            if (typed <= 0.0 && kind != ConstraintKind::Angle) {
                emit message(tr("Dimensions must be greater than zero"));
                return;
            }
            commit(tr("Edit Dimension"), build(typed));
        },
        build);
}

void SketchEditor::promptValue(QPointF pos, double initial, core::Quantity quantity, bool centered,
                               std::function<void(double)> apply, std::function<sketch::Sketch(double)> preview)
{
    m_valueApply = std::move(apply);
    m_valuePreview = std::move(preview);
    m_valueQuantity = quantity;
    m_valueEditor->setText(formatValue(initial));
    previewValue(m_valueEditor->text());
    const int height = m_valueEditor->sizeHint().height();
    const QPoint at = centered ? pos.toPoint() - QPoint(m_valueEditor->width() / 2, height / 2)
                               : pos.toPoint() + QPoint(12, -height - 4);
    m_valueEditor->move(std::clamp(at.x(), 0, std::max(0, m_view->width() - m_valueEditor->width())),
                        std::clamp(at.y(), 0, std::max(0, m_view->height() - m_valueEditor->height())));
    m_valueEditor->show();
    m_valueEditor->raise();
    m_valueEditor->setFocus();
    m_valueEditor->selectAll();
}

void SketchEditor::previewValue(const QString& text)
{
    if (!m_valuePreview) {
        return;
    }
    std::optional<sketch::Sketch> shown;
    if (const std::optional<double> typed = parseValue(text, m_valueQuantity)) {
        sketch::Sketch candidate = m_valuePreview(*typed);
        const sketch::SolveResult result = sketch::solve(candidate);
        if (result.solved && result.conflicting.empty()) {
            shown = std::move(candidate);
        }
    }
    m_dragPreview = std::move(shown);
    m_previewingValue = true;
    redraw();
}

void SketchEditor::closeValueEditor()
{
    m_valueApply = nullptr;
    m_valuePreview = nullptr;
    if (std::exchange(m_previewingValue, false)) {
        m_dragPreview.reset();
        redraw();
    }
    if (m_valueEditor->isVisible()) {
        m_valueEditor->hide();
        m_view->setFocus();
    }
}

void SketchEditor::resetProgress()
{
    m_first.reset();
    m_second.reset();
    m_picks.clear();
    m_pending.reset();
    m_arcSweep = 0.0;
    m_chainLine = 0;
    if (m_mirrorAxis != 0) {
        m_selection.erase(m_mirrorAxis);
        m_mirrorAxis = 0;
    }
    m_inputs->close();
}

void SketchEditor::dropDrag()
{
    m_dragPoint = 0;
    m_dragStart.reset();
    m_dragPreview.reset();
    dropBox();
}

void SketchEditor::dropBox()
{
    m_boxStart.reset();
    if (std::exchange(m_boxing, false)) {
        m_view->hideBand();
    }
}

void SketchEditor::selectInBox(QPointF from, QPointF to)
{
    const gp_Ax3 ax = plane();
    const auto project = [&](Vec2 p) {
        const QPointF screen = m_view->toScreen(sketch::toWorld(ax, p));
        return Vec2{screen.x(), screen.y()};
    };
    const sketch::BoxMode mode = viewer::isCrossingBox(from, to) ? sketch::BoxMode::Crossing : sketch::BoxMode::Window;
    const std::vector<Id> ids = sketch::inBox(current(), project, {from.x(), from.y()}, {to.x(), to.y()}, mode);
    m_selection.insert(ids.begin(), ids.end());
    K137_DEBUG(Input, "{} box picked {} sketch items", mode == sketch::BoxMode::Crossing ? "Crossing" : "Window",
               ids.size());
    redraw();
    emit stateChanged();
}

} // namespace kalve137::app
