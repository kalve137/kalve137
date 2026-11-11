// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/document.h"
#include "core/expression.h"
#include "sketch/sketch.h"
#include "sketch/solver.h"

#include <AIS_InteractiveObject.hxx>
#include <gp_Ax3.hxx>

#include <QObject>
#include <QPointF>
#include <QString>

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

class QLineEdit;
class QTimer;

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::app {

class DesignSession;
class SketchInputs;

class SketchEditor : public QObject {
    Q_OBJECT

public:
    enum class Tool {
        Select,
        Line,
        Rectangle,
        CenterRectangle,
        Circle,
        Arc,
        ArcThrough,
        Polygon,
        Point,
        Trim,
        Fillet,
        Mirror,
        Dimension,
        Coincident,
        Horizontal,
        Vertical,
        Parallel,
        Perpendicular,
        Tangent,
        Equal,
        Midpoint,
        Symmetric,
        Fix,
    };

    SketchEditor(DesignSession& session, viewer::OcctViewWidget* view, QObject* parent = nullptr);
    ~SketchEditor() override;

    void begin(core::FeatureId sketch);
    void end();
    bool isActive() const { return m_sketchId != 0; }
    core::FeatureId sketchId() const { return m_sketchId; }

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void cancel();
    void deleteSelection();
    void toggleConstruction();

    QString hint() const;
    QString solveSummary() const;
    QString selectionSummary() const;
    const sketch::Sketch& shownSketch() const;
    struct Display {
        bool profiles = true;
        bool points = true;
        bool dimensions = true;
        bool constraints = true;
        bool construction = true;

        bool operator==(const Display&) const = default;
    };
    void setDisplay(const Display& display);
    void setGridSnap(bool snap) { m_gridSnap = snap; }
    void setMoveIncrement(double increment) { m_moveIncrement = increment; }

signals:
    void toolChanged();
    void message(const QString& text);
    void stateChanged();
    void finished();

private:
    struct Snap {
        sketch::Vec2 pos;
        sketch::Id point = 0;
        sketch::Id midpointOf = 0;
        sketch::Id onCurve = 0;
        bool onGrid = false;
        std::optional<sketch::Vec2> alignedX;
        std::optional<sketch::Vec2> alignedY;
        std::optional<sketch::Vec2> extendedFrom;
    };

    struct Hit {
        enum class Kind { None, Point, Curve, Constraint };
        Kind kind = Kind::None;
        sketch::Id id = 0;
    };

    struct PendingDimension {
        sketch::ConstraintKind kind = sketch::ConstraintKind::Distance;
        std::vector<sketch::Id> refs;
        double value = 0.0;
        bool axisFromCursor = false;
    };

    void onPress(QPointF pos, Qt::KeyboardModifiers modifiers);
    void onRelease(QPointF pos);
    void onMove(QPointF pos, Qt::MouseButtons buttons);
    void onDoubleClick(QPointF pos);
    void onSessionChanged();

    static bool drawsGeometry(Tool tool);

    const core::SketchFeature* feature() const;
    const sketch::Sketch& current() const;
    gp_Ax3 plane() const;
    std::optional<sketch::Vec2> toSketch(QPointF pos) const;
    double tolerance() const;

    Snap snapAt(sketch::Vec2 pos, sketch::Id ignorePoint = 0) const;
    void extendAlongLine(sketch::Vec2 pos, sketch::Id ignorePoint, Snap& snap) const;
    static sketch::Id placePoint(sketch::Sketch& sketch, const Snap& snap);
    Hit geometryAt(sketch::Vec2 pos) const;
    Hit curveAt(sketch::Vec2 pos) const;
    std::optional<sketch::Id> cornerAt(sketch::Vec2 pos) const;
    Hit hitAt(QPointF pos) const;
    std::optional<sketch::Id> detectedConstraint() const;

    void clickLine(Snap snap);
    void clickRectangle(Snap snap);
    void clickCenterRectangle(Snap snap);
    void clickCircle(Snap snap);
    void clickPolygon(Snap snap);
    void clickArcThrough(const Snap& snap);
    void clickPoint(const Snap& snap);

    void openInputs();
    void onInputsAccepted();
    bool anyHeld() const;
    std::optional<double> heldPositive(std::size_t index) const;
    std::optional<sketch::Vec2> chainDirection() const;
    sketch::Vec2 protractorReference() const;
    sketch::Vec2 draftLineEnd(sketch::Vec2 cursor) const;
    sketch::Vec2 draftCorner(sketch::Vec2 cursor) const;
    sketch::Vec2 draftCenterCorner(sketch::Vec2 cursor) const;
    sketch::Vec2 draftPolygonCorner(sketch::Vec2 cursor) const;
    int polygonSides() const;
    double draftRadius(sketch::Vec2 cursor) const;
    void placeInputs(sketch::Vec2 cursor);
    void clickArc(const Snap& snap);
    void clickTrim(sketch::Vec2 pos);
    void clickFillet(QPointF screen, sketch::Vec2 pos);
    void clickMirror(sketch::Vec2 pos);
    void clickDimension(QPointF pos, const Hit& hit);
    void clickConstraint(const Hit& hit);
    void clickSelect(const Hit& hit, Qt::KeyboardModifiers modifiers);
    void selectInBox(QPointF from, QPointF to);
    void dropBox();

    bool commit(const QString& label, sketch::Sketch candidate);
    void startDimension(sketch::ConstraintKind kind, std::vector<sketch::Id> refs, double value,
                        bool axisFromCursor = false);
    std::optional<sketch::Constraint> pendingConstraint(sketch::Vec2 cursor) const;
    void placeDimension(QPointF pos, sketch::Vec2 at);
    void addDimension(sketch::Constraint constraint, QPointF pos);
    void editDimension(sketch::Id constraint, QPointF pos);
    void promptValue(QPointF pos, double initial, core::Quantity quantity, bool centered,
                     std::function<void(double)> apply, std::function<sketch::Sketch(double)> preview = {});
    void previewValue(const QString& text);
    void closeValueEditor();
    void resetProgress();
    void dropDrag();

    void redraw();
    void redrawPreview();
    void clearObjects(std::vector<Handle(AIS_InteractiveObject)>& objects);
    void drawConstraints(const sketch::Sketch& sketch, const sketch::SolveResult& solve);
    void show(const Handle(AIS_InteractiveObject)& object, std::vector<Handle(AIS_InteractiveObject)>& into,
              bool selectable);

    DesignSession& m_session;
    viewer::OcctViewWidget* m_view;
    core::FeatureId m_sketchId = 0;
    Tool m_tool = Tool::Select;
    sketch::SolveResult m_solve;
    double m_drawnScale = 0.0;
    QTimer* m_rescale = nullptr;
    Display m_display;
    bool m_gridSnap = true;
    double m_moveIncrement = 0.0;

    std::set<sketch::Id> m_selection;
    sketch::Id m_hover = 0;
    sketch::Vec2 m_cursor;
    Snap m_cursorSnap;
    bool m_hasCursor = false;

    std::optional<Snap> m_first;
    std::optional<Snap> m_second;
    double m_arcSweep = 0.0;
    double m_arcLastAngle = 0.0;
    double m_filletRadius = 2.0;
    int m_polygonSides = 6;
    sketch::Id m_mirrorAxis = 0;
    std::vector<Hit> m_picks;
    std::optional<PendingDimension> m_pending;

    std::optional<QPointF> m_boxStart;
    bool m_boxing = false;

    sketch::Id m_dragPoint = 0;
    std::optional<sketch::Sketch> m_dragStart;
    std::optional<sketch::Sketch> m_dragPreview;
    bool m_committing = false;

    QLineEdit* m_valueEditor = nullptr;
    std::function<void(double)> m_valueApply;
    std::function<sketch::Sketch(double)> m_valuePreview;
    bool m_previewingValue = false;
    core::Quantity m_valueQuantity = core::Quantity::Length;
    SketchInputs* m_inputs = nullptr;
    sketch::Id m_chainLine = 0;

    std::vector<Handle(AIS_InteractiveObject)> m_objects;
    std::vector<Handle(AIS_InteractiveObject)> m_preview;
    std::map<const AIS_InteractiveObject*, sketch::Id> m_constraintObjects;
};

} // namespace kalve137::app
