// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/feature/recent_values.h"
#include "app/feature/value_edit.h"
#include "app/session/design_session.h"
#include "app/sketch/sketch_editor_detail.h"
#include "app/sketch/sketch_inputs.h"
#include "app/theme/fonts.h"
#include "app/theme/theme.h"
#include "app/view/band_overlay.h"
#include "core/parameter_table.h"
#include "core/projection.h"

#include "geometry/shapes.h"
#include "log/log.h"
#include "render/rhi/rhi_view.h"
#include "sketch/dimension.h"
#include "sketch/dimension_layout.h"
#include "sketch/draft.h"
#include "sketch/offset.h"
#include "sketch/profiles.h"
#include "sketch/select.h"
#include "viewer/box_band.h"
#include "viewer/grid.h"
#include "viewer/occt_view_widget.h"

#include <PrsDim_Dimension.hxx>
#include <gp_Pln.hxx>

#include <QFont>
#include <QFontMetricsF>
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

SketchEditor::SketchEditor(DesignSession& session, const Scene& scene, viewer::OcctViewWidget* view,
                           render::RhiView* newView, QObject* parent)
    : QObject(parent), m_session(session), m_scene(scene), m_view(view), m_newView(newView),
      m_cameraInput(newView != nullptr)
{
    m_valueEditor = new QLineEdit(canvas());
    m_valueEditor->setObjectName("dimensionEditor");
    m_valueEditor->setFixedWidth(kValueEditorWidth);
    m_valueEditor->hide();
    m_recentValues = new RecentValuesMenu(
        m_valueEditor, [this] { return m_valueQuantity; },
        [this](double value) {
            m_valueEditor->setText(formatValue(value));
            m_valueEditor->selectAll();
            previewValue(m_valueEditor->text());
        });
    m_valueNames = new NameCompleter(m_valueEditor, [this]() -> std::optional<core::ParameterValues> {
        if (!m_valueTakesNames) {
            return std::nullopt;
        }
        return core::parameterValues(m_session.document());
    });
    m_valueError = new FieldError(m_valueEditor);
    connect(m_valueEditor, &QLineEdit::returnPressed, this, [this] {
        if (m_valueNames->justCompleted()) {
            return;
        }
        m_valueNames->hide();
        const TypedValue typed = readTyped(m_valueEditor->text(), m_valueQuantity, valueNames());
        if (typed.error) {
            if (m_valueTakesNames) {
                m_valueError->showError(*typed.error);
            } else {
                emit message(tr("Type a number or an expression, such as 30/2 or 1in"));
            }
            return;
        }
        m_recentValues->commit(typed.value);
        std::function<void(double, const std::string&)> apply = std::move(m_valueApply);
        closeValueEditor();
        if (apply) {
            apply(typed.value, typed.canonical);
        }
    });
    connect(theme::notifier(), &theme::Notifier::changed, this, [this] {
        if (isActive()) {
            redraw();
        }
    });
    connect(m_valueEditor, &QLineEdit::textEdited, this, &SketchEditor::previewValue);
    connect(m_valueEditor, &QLineEdit::editingFinished, this, [this] {
        if (!m_valueEditor->hasFocus()) {
            closeValueEditor();
        }
    });

    m_inputs = new SketchInputs(canvas(), this);
    connect(m_inputs, &SketchInputs::edited, this, [this] {
        if (m_typedMove) {
            previewTypedMove();
        } else {
            redrawPreview();
        }
    });
    connect(m_inputs, &SketchInputs::accepted, this, &SketchEditor::onInputsAccepted);

    connect(&session, &DesignSession::changed, this, &SketchEditor::onSessionChanged);
    if (newView != nullptr) {
        m_band = new BandOverlay(newView);
        connect(newView, &render::RhiView::capturedPress, this, &SketchEditor::onPress);
        connect(newView, &render::RhiView::capturedRelease, this, &SketchEditor::onRelease);
        connect(newView, &render::RhiView::capturedDoubleClick, this, &SketchEditor::onDoubleClick);
        connect(newView, &render::RhiView::mouseMoved, this,
                [this](QPointF pos, Qt::MouseButtons buttons, Qt::KeyboardModifiers) { onMove(pos, buttons); });
        connect(newView, &render::RhiView::cameraChanged, this, &SketchEditor::onCameraChanged);
        return;
    }
    connect(view, &viewer::OcctViewWidget::capturedPress, this, &SketchEditor::onPress);
    connect(view, &viewer::OcctViewWidget::capturedRelease, this, &SketchEditor::onRelease);
    connect(view, &viewer::OcctViewWidget::capturedDoubleClick, this, &SketchEditor::onDoubleClick);
    connect(view, &viewer::OcctViewWidget::pointerMoved, this, &SketchEditor::onMove);

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
    seed();
    m_tool = Tool::Select;
    m_repick = 0;
    if (m_newView != nullptr) {
        m_newView->setLeftButtonCaptured(true);
    } else {
        m_view->setLeftButtonCaptured(true);
    }
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
    m_before.reset();
    m_drawCache.reset();
    m_repick = 0;
    canvas()->unsetCursor();
    if (m_newView != nullptr) {
        clearView();
        m_newView->setLeftButtonCaptured(false);
    } else {
        m_view->setLeftButtonCaptured(false);
        m_view->redraw();
    }
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
    m_repick = 0;
    setModelPicking(tool == Tool::Project);
    if (drawsGeometry(tool)) {
        canvas()->setCursor(Qt::CrossCursor);
    } else {
        canvas()->unsetCursor();
    }
    m_offsetChain.clear();
    if (tool != Tool::Select && tool != Tool::Mirror && tool != Tool::Offset) {
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
    if (m_typedMove) {
        resetProgress();
        redraw();
        emit stateChanged();
        return;
    }
    if (!m_dragPoints.empty() || m_dragLabel != 0) {
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
    if (m_typedMove) {
        return tr("Type X and Y offsets, Tab to next value, Enter to move, Esc to stop");
    }
    switch (m_tool) {
    case Tool::Select:
        return tr("Click to select, drag geometry to move, double-click a dimension to edit it");
    case Tool::Line:
        return m_first ? tr("Type a length, Tab to next value, Enter to finish, Esc to stop")
                       : tr("Click the start point");
    case Tool::Rectangle:
        return m_first ? tr("Click the opposite corner, or type the width, Tab, the height and Enter")
                       : tr("Click the first corner");
    case Tool::CenterRectangle:
        return m_first ? tr("Click a corner, or type the width, Tab, the height and Enter") : tr("Click the center");
    case Tool::Polygon:
        return m_first ? tr("Click a corner, or type the diameter, Tab, the number of sides and Enter")
                       : tr("Click the center");
    case Tool::Slot:
        if (!m_first) {
            return tr("Click the first arc center");
        }
        return m_second ? tr("Click to set the width, or type it and Enter")
                        : tr("Click the second arc center, or type the length, Tab, the width and Enter");
    case Tool::CenterSlot:
        if (!m_first) {
            return tr("Click the slot center");
        }
        return m_second ? tr("Click to set the width, or type it and Enter")
                        : tr("Click an arc center, or type the length, Tab, the width and Enter");
    case Tool::OverallSlot:
        if (!m_first) {
            return tr("Click one end of the slot");
        }
        return m_second ? tr("Click to set the width, or type it and Enter")
                        : tr("Click the other end, or type the length, Tab, the width and Enter");
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
    case Tool::Offset:
        return m_offsetChain.empty()
                   ? tr("Click a curve to offset it with the curves joined to it")
                   : tr("Type the distance and Enter, move the cursor to the other side or type a negative "
                        "distance to flip");
    case Tool::Project:
        return m_repick != 0 ? tr("Click the edge or vertex the selected curve should follow, Esc to cancel")
                             : tr("Click edges, vertices or faces to project them, Esc to finish");
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
    case Tool::Concentric:
        return tr("Click two circles or arcs");
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
    case Tool::Slot:
    case Tool::CenterSlot:
    case Tool::OverallSlot:
    case Tool::Point:
        return true;
    case Tool::Select:
    case Tool::Trim:
    case Tool::Fillet:
    case Tool::Mirror:
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
    return m_current;
}

void SketchEditor::seed()
{
    const core::SketchFeature* data = feature();
    m_current = data->sketch;
    if (data->projections.empty()) {
        return;
    }
    const auto state = m_session.model().sketches.find(m_sketchId);
    if (state == m_session.model().sketches.end()) {
        return;
    }
    const sketch::Sketch& solved = state->second.solved;
    for (const auto& [id, from] : solved.points()) {
        if (sketch::Point* to = m_current.point(id)) {
            to->pos = from.pos;
        }
    }
    for (const auto& [id, from] : solved.curves()) {
        if (sketch::Curve* to = m_current.curve(id)) {
            to->radius = from.radius;
        }
    }
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
    if (m_typedMove) {
        resetProgress();
        redraw();
    }
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
        } else if (hit.kind == Hit::Kind::Constraint) {
            startLabelDrag(hit.id, pos, *p);
        } else if (m_selection.contains(hit.id)) {
            startGroupDrag(pos, *p, movablePoints());
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
    case Tool::Slot:
    case Tool::CenterSlot:
    case Tool::OverallSlot:
        clickSlot(snapAt(*p));
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
    case Tool::Offset:
        clickOffset(pos, *p);
        break;
    case Tool::Project:
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
    case Tool::Concentric:
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
    if (m_dragPoints.empty() && m_dragLabel == 0) {
        return;
    }
    const QString label = m_dragLabel != 0 ? tr("Move Dimension") : tr("Move");
    m_dragPoints.clear();
    m_draggingGroup = false;
    m_dragLabel = 0;
    m_draggingLabel = false;
    std::optional<sketch::Sketch> moved = std::move(m_dragPreview);
    m_dragPreview.reset();
    m_dragStart.reset();
    if (moved) {
        commit(label, std::move(*moved));
    } else {
        redraw();
    }
}

void SketchEditor::onMove(QPointF pos, Qt::MouseButtons buttons)
{
    if (!isActive() || feature() == nullptr || (buttons & ~Qt::MouseButtons(Qt::LeftButton)) != Qt::NoButton) {
        return;
    }
    if (m_boxStart && (buttons & Qt::LeftButton) && (m_boxing || viewer::isBoxDrag(*m_boxStart, pos))) {
        m_boxing = true;
        if (m_band != nullptr) {
            m_band->setColor(theme::palette().chrome.accent);
            m_band->showBand(*m_boxStart, pos);
        } else {
            m_view->showBand(*m_boxStart, pos);
        }
        return;
    }
    const std::optional<Vec2> p = toSketch(pos);
    if (!p) {
        return;
    }
    m_cursor = *p;
    m_hasCursor = true;

    if (m_dragLabel != 0 && (buttons & Qt::LeftButton) && m_dragStart) {
        dragLabel(pos, *p);
        return;
    }
    if (!m_dragPoints.empty() && !m_typedMove && (buttons & Qt::LeftButton) && m_dragStart) {
        dragGroup(pos, *p);
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
    case Tool::Slot:
    case Tool::CenterSlot:
    case Tool::OverallSlot:
    case Tool::Point:
        m_cursorSnap = snapAt(*p);
        break;
    case Tool::Select:
    case Tool::Trim:
    case Tool::Fillet:
    case Tool::Mirror:
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
        m_cursorSnap = Snap{*p};
        break;
    }

    if (m_tool == Tool::Offset && !m_offsetChain.empty()) {
        if (const std::optional<sketch::Chain> chain = sketch::orderChain(current(), m_offsetChain)) {
            const int side = sketch::offsetSide(current(), *chain, *p);
            if (side != 0 && side != m_offsetSide) {
                m_offsetSide = side;
                previewValue(m_valueEditor->text());
            }
        }
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
        redrawHover();
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
    if (const std::optional<Id> constraint = constraintAt(pos)) {
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
    seed();
    if (!m_committing) {
        m_before.reset();
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
    redraw();
}

std::optional<QPointF> SketchEditor::labelOnScreen(Id constraint) const
{
    if (m_cameraInput) {
        const sketch::Sketch& s = shownSketch();
        const sketch::Constraint* c = s.constraint(constraint);
        const std::optional<sketch::DimensionLayout> layout =
            c != nullptr ? sketch::layoutDimension(s, *c, worldPerPixel(), sketch::dimensionMiddle(s)) : std::nullopt;
        if (!layout) {
            return std::nullopt;
        }
        return toScreen(sketch::toWorld(plane(), layout->text));
    }
    for (const auto& [object, id] : m_constraintObjects) {
        if (const auto* dimension = dynamic_cast<const PrsDim_Dimension*>(object);
            dimension != nullptr && id == constraint) {
            return m_view->toScreen(dimension->GetTextPosition());
        }
    }
    return std::nullopt;
}

void SketchEditor::startLabelDrag(Id constraint, QPointF pos, Vec2 at)
{
    const sketch::Constraint* c = current().constraint(constraint);
    if (c == nullptr || !sketch::isDimension(c->kind)) {
        return;
    }
    const std::optional<QPointF> screen = labelOnScreen(constraint);
    const std::optional<Vec2> label = screen ? toSketch(*screen) : std::nullopt;
    if (!label) {
        return;
    }
    m_dragLabel = constraint;
    m_dragLabelPress = pos;
    m_dragLabelGrab = at;
    m_dragLabelStart = *label;
    m_draggingLabel = false;
    m_dragStart = current();
    m_dragPreview.reset();
}

void SketchEditor::dragLabel(QPointF pos, Vec2 at)
{
    if (!m_draggingLabel && !viewer::isBoxDrag(m_dragLabelPress, pos)) {
        return;
    }
    m_draggingLabel = true;
    sketch::Sketch candidate = *m_dragStart;
    sketch::Constraint* c = candidate.constraint(m_dragLabel);
    const std::optional<Vec2> anchor = c != nullptr ? sketch::dimensionAnchor(candidate, *c) : std::nullopt;
    if (!anchor) {
        return;
    }
    c->labelOffset = m_dragLabelStart + (at - m_dragLabelGrab) - *anchor;
    m_dragPreview = std::move(candidate);
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
    const bool committed = m_session.edit(label, [id, &candidate](core::Document& document) {
        if (core::Feature* f = document.find(id)) {
            auto& data = std::get<core::SketchFeature>(f->data);
            data.sketch = std::move(candidate);
            core::dropBrokenProjections(data);
        }
    });
    if (!committed) {
        redraw();
    }
    return committed;
}

void SketchEditor::addDimension(sketch::Constraint constraint, QPointF pos)
{
    const ConstraintKind kind = constraint.kind;
    const double value = constraint.value;
    const Id label = current().nextId();
    const auto build = [this, constraint = std::move(constraint)](double typed, const std::string& expression) {
        sketch::Sketch candidate = current();
        const Id id = candidate.addConstraint(constraint.kind, constraint.refs, typed);
        candidate.constraint(id)->labelOffset = constraint.labelOffset;
        candidate.constraint(id)->expression = expression;
        return candidate;
    };
    promptValue(
        pos, value, quantityOf(kind), label,
        [this, kind, build](double typed, const std::string& expression) {
            if (typed <= 0.0 && kind != ConstraintKind::Angle) {
                emit message(tr("Dimensions must be greater than zero"));
                return;
            }
            commit(tr("Dimension"), build(typed, expression));
        },
        [build](double typed) { return build(typed, {}); },
        core::takesExpression(kind) ? std::optional(QString()) : std::nullopt);
}

void SketchEditor::editDimension(Id constraint, QPointF pos)
{
    const sketch::Constraint* c = current().constraint(constraint);
    if (c == nullptr || !sketch::isDimension(c->kind)) {
        return;
    }
    const ConstraintKind kind = c->kind;
    if (const std::optional<QPointF> label = labelOnScreen(constraint)) {
        pos = *label;
    }
    const auto build = [this, constraint](double typed, const std::string& expression) {
        sketch::Sketch candidate = current();
        if (sketch::Constraint* edited = candidate.constraint(constraint)) {
            edited->value = typed;
            edited->expression = expression;
        }
        return candidate;
    };
    const std::optional<QString> expression =
        core::takesExpression(kind) ? std::optional(QString::fromStdString(c->expression)) : std::nullopt;
    promptValue(
        pos, c->value, quantityOf(kind), constraint,
        [this, kind, build](double typed, const std::string& typedExpression) {
            if (typed <= 0.0 && kind != ConstraintKind::Angle) {
                emit message(tr("Dimensions must be greater than zero"));
                return;
            }
            sketch::Sketch candidate = build(typed, typedExpression);
            if (kind == ConstraintKind::Offset) {
                sketch::Sketch probe = candidate;
                if (!sketch::solve(probe).solved) {
                    emit message(tr("A %1 mm offset doesn't fit these curves").arg(formatValue(typed)));
                    return;
                }
            }
            commit(tr("Edit Dimension"), std::move(candidate));
        },
        [build](double typed) { return build(typed, {}); }, expression);
}

ParameterSource SketchEditor::valueNames() const
{
    if (!m_valueTakesNames) {
        return {};
    }
    return [this] { return core::parameterValues(m_session.document()); };
}

void SketchEditor::promptValue(QPointF pos, double initial, core::Quantity quantity, Id label,
                               std::function<void(double, const std::string&)> apply,
                               std::function<sketch::Sketch(double)> preview, std::optional<QString> expression)
{
    m_valueApply = std::move(apply);
    m_valuePreview = std::move(preview);
    m_valueQuantity = quantity;
    m_editingLabel = label;
    m_valueTakesNames = expression.has_value();
    m_valueError->dismiss();
    m_valueEditor->setText(expression && !expression->isEmpty() ? *expression : formatValue(initial));
    m_recentValues->reset();
    previewValue(m_valueEditor->text());
    const int trailing = recentValues(quantity).empty() ? 0 : kRecentValuesPixels;
    QPoint at;
    if (label != 0 && m_editedLabel) {
        QFont font(fonts::families().qt);
        font.setPixelSize(static_cast<int>(kDimensionTextPixels));
        m_valueEditor->setFont(font);
        const QFontMetricsF metrics(font);
        const double room = metrics.horizontalAdvance(QStringLiteral("0000.00")) + 2.0 * theme::kSpaceS + trailing;
        const QSize size(
            static_cast<int>(std::ceil(std::max(m_editedLabel->width(), room))),
            static_cast<int>(std::ceil(std::max(m_editedLabel->height(), double(m_valueEditor->sizeHint().height())))));
        m_valueEditor->setFixedSize(size);
        m_valueEditor->setAlignment(Qt::AlignCenter);
        at = (m_editedLabel->center() - QPointF(size.width(), size.height()) / 2.0).toPoint();
    } else {
        m_valueEditor->setFont(canvas()->font());
        m_valueEditor->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_valueEditor->setFixedSize(kValueEditorWidth + trailing, m_valueEditor->sizeHint().height());
        at = pos.toPoint() + QPoint(static_cast<int>(kInputGapPixels), -m_valueEditor->height() - theme::kSpaceXs);
    }
    m_valueEditor->move(std::clamp(at.x(), 0, std::max(0, canvas()->width() - m_valueEditor->width())),
                        std::clamp(at.y(), 0, std::max(0, canvas()->height() - m_valueEditor->height())));
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
    m_valueError->dismiss();
    std::optional<sketch::Sketch> shown;
    if (const TypedValue typed = readTyped(text, m_valueQuantity, valueNames()); !typed.error) {
        sketch::Sketch candidate = m_valuePreview(typed.value);
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
    m_offsetChain.clear();
    m_valueApply = nullptr;
    m_valuePreview = nullptr;
    m_valueTakesNames = false;
    m_valueError->dismiss();
    m_valueNames->hide();
    const bool labelHidden = std::exchange(m_editingLabel, 0) != 0;
    if (std::exchange(m_previewingValue, false)) {
        m_dragPreview.reset();
        redraw();
    } else if (labelHidden) {
        redraw();
    }
    if (m_valueEditor->isVisible()) {
        m_valueEditor->hide();
        canvas()->setFocus();
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
    if (std::exchange(m_typedMove, false)) {
        m_dragPoints.clear();
        m_dragStart.reset();
        m_dragPreview.reset();
    }
}

void SketchEditor::dropDrag()
{
    m_dragPoints.clear();
    m_draggingGroup = false;
    m_dragLabel = 0;
    m_draggingLabel = false;
    m_dragStart.reset();
    m_dragPreview.reset();
    dropBox();
}

void SketchEditor::dropBox()
{
    m_boxStart.reset();
    if (std::exchange(m_boxing, false)) {
        if (m_band != nullptr) {
            m_band->hideBand();
        } else {
            m_view->hideBand();
        }
    }
}

void SketchEditor::selectInBox(QPointF from, QPointF to)
{
    const gp_Ax3 ax = plane();
    const auto project = [&](Vec2 p) {
        const QPointF screen = toScreen(sketch::toWorld(ax, p));
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
