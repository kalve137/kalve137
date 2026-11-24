// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "core/document.h"
#include "core/expression.h"
#include "render/annotations.h"
#include "render/sketches.h"
#include "sketch/dimension_layout.h"
#include "sketch/sketch.h"
#include "sketch/solver.h"

#include <AIS_InteractiveObject.hxx>
#include <gp_Ax3.hxx>

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

class QLineEdit;
class QTimer;
class QWidget;

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::render {
class RhiView;
}

namespace kalve137::app {

class BandOverlay;
class DesignSession;
class RecentValuesMenu;
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
        Slot,
        CenterSlot,
        OverallSlot,
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
        Concentric,
        Equal,
        Midpoint,
        Symmetric,
        Fix,
    };

    struct Hit {
        enum class Kind { None, Point, Curve, Constraint };
        Kind kind = Kind::None;
        sketch::Id id = 0;

        bool operator==(const Hit&) const = default;
    };

    SketchEditor(DesignSession& session, viewer::OcctViewWidget* view, render::RhiView* newView,
                 QObject* parent = nullptr);
    ~SketchEditor() override;

    void begin(core::FeatureId sketch);
    void end();
    bool isActive() const { return m_sketchId != 0; }
    core::FeatureId sketchId() const { return m_sketchId; }

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void cancel();
    void startTypedMove();
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
    void setCameraInput(bool camera) { m_cameraInput = camera; }
    bool hasCameraInput() const { return m_cameraInput; }
    Hit hitAt(QPointF pos) const;
    void redraw();

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

    struct Projection {
        camera::Camera camera;
        camera::Viewport viewport;
    };

    struct ViewItems {
        snapshot::SketchProfiles profiles;
        std::vector<render::SketchStroke> halos;
        std::vector<render::SketchStroke> lines;
        std::vector<render::SketchTriangle> arrows;
        std::vector<render::MarkerItem> markers;
        render::Annotations annotations;
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
    QWidget* canvas() const;
    std::optional<Projection> projection() const;
    double worldPerPixel() const;
    QPointF toScreen(const gp_Pnt& point) const;
    double gridStep() const;
    std::optional<sketch::Vec2> toSketch(QPointF pos) const;
    double tolerance() const;

    Snap snapAt(sketch::Vec2 pos, sketch::Id ignorePoint = 0) const;
    void extendAlongLine(sketch::Vec2 pos, sketch::Id ignorePoint, Snap& snap) const;
    static sketch::Id placePoint(sketch::Sketch& sketch, const Snap& snap);
    Hit geometryAt(sketch::Vec2 pos) const;
    Hit curveAt(sketch::Vec2 pos) const;
    std::optional<sketch::Id> cornerAt(sketch::Vec2 pos) const;
    std::optional<sketch::Id> constraintAt(QPointF pos) const;
    std::optional<sketch::Id> detectedConstraint() const;
    std::optional<sketch::Id> constraintOnScreen(QPointF pos) const;
    QRectF labelRect(const sketch::DimensionLayout& layout) const;

    void clickLine(Snap snap);
    void clickRectangle(Snap snap);
    void clickCenterRectangle(Snap snap);
    void clickCircle(Snap snap);
    void clickPolygon(Snap snap);
    void clickSlot(Snap snap);
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
    sketch::Vec2 draftSlotEnd(sketch::Vec2 cursor) const;
    double draftSlotWidth(sketch::Vec2 cursor) const;
    std::optional<std::pair<sketch::Vec2, sketch::Vec2>> slotCenters(sketch::Vec2 end, double width) const;
    double draftRadius(sketch::Vec2 cursor) const;
    void placeInputs(sketch::Vec2 cursor);
    void clickArc(const Snap& snap);
    void clickTrim(sketch::Vec2 pos);
    void clickFillet(QPointF screen, sketch::Vec2 pos);
    void clickMirror(sketch::Vec2 pos);
    void clickDimension(QPointF pos, const Hit& hit);
    void clickConstraint(const Hit& hit);
    void clickSelect(const Hit& hit, Qt::KeyboardModifiers modifiers);
    void startLabelDrag(sketch::Id constraint, QPointF pos, sketch::Vec2 at);
    void dragLabel(QPointF pos, sketch::Vec2 at);
    std::optional<QPointF> labelOnScreen(sketch::Id constraint) const;
    void selectInBox(QPointF from, QPointF to);
    void dropBox();
    std::vector<sketch::Id> movablePoints() const;
    void startGroupDrag(QPointF pos, sketch::Vec2 at, std::vector<sketch::Id> points);
    void dragGroup(QPointF pos, sketch::Vec2 at);
    bool moveBy(sketch::Vec2 delta);
    void placeMoveInputs(const sketch::Sketch& s);
    void previewTypedMove();
    void acceptTypedMove();

    bool commit(const QString& label, sketch::Sketch candidate);
    void startDimension(sketch::ConstraintKind kind, std::vector<sketch::Id> refs, double value,
                        bool axisFromCursor = false);
    std::optional<sketch::Constraint> pendingConstraint(sketch::Vec2 cursor) const;
    void placeDimension(QPointF pos, sketch::Vec2 at);
    void addDimension(sketch::Constraint constraint, QPointF pos);
    void editDimension(sketch::Id constraint, QPointF pos);
    void promptValue(QPointF pos, double initial, core::Quantity quantity, sketch::Id label,
                     std::function<void(double)> apply, std::function<sketch::Sketch(double)> preview = {});
    void previewValue(const QString& text);
    void closeValueEditor();
    void resetProgress();
    void dropDrag();

    void redrawPreview();
    void clearObjects(std::vector<Handle(AIS_InteractiveObject)>& objects);
    void showUnder(const Handle(AIS_InteractiveObject)& halo);
    void drawSketch(const sketch::Sketch& sketch, const std::set<sketch::Id>& conflicted);
    void drawConstraints(const sketch::Sketch& sketch, const sketch::SolveResult& solve);
    void drawViewSketch(const sketch::Sketch& sketch, const std::set<sketch::Id>& conflicted);
    void drawViewConstraints(const sketch::Sketch& sketch, const sketch::SolveResult& solve);
    void clearView();
    void publish();
    void onCameraChanged();
    void show(const Handle(AIS_InteractiveObject)& object, std::vector<Handle(AIS_InteractiveObject)>& into,
              bool selectable);

    DesignSession& m_session;
    viewer::OcctViewWidget* m_view;
    render::RhiView* m_newView;
    bool m_cameraInput = false;
    BandOverlay* m_band = nullptr;
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

    std::vector<sketch::Id> m_dragPoints;
    QPointF m_dragPress;
    sketch::Vec2 m_dragGrab;
    bool m_draggingGroup = false;
    bool m_typedMove = false;
    sketch::Id m_dragLabel = 0;
    QPointF m_dragLabelPress;
    sketch::Vec2 m_dragLabelGrab;
    sketch::Vec2 m_dragLabelStart;
    bool m_draggingLabel = false;
    std::optional<sketch::Sketch> m_dragStart;
    std::optional<sketch::Sketch> m_dragPreview;
    bool m_committing = false;

    QLineEdit* m_valueEditor = nullptr;
    RecentValuesMenu* m_recentValues = nullptr;
    std::function<void(double)> m_valueApply;
    std::function<sketch::Sketch(double)> m_valuePreview;
    bool m_previewingValue = false;
    sketch::Id m_editingLabel = 0;
    std::optional<QRectF> m_editedLabel;
    core::Quantity m_valueQuantity = core::Quantity::Length;
    SketchInputs* m_inputs = nullptr;
    sketch::Id m_chainLine = 0;

    std::vector<Handle(AIS_InteractiveObject)> m_objects;
    std::vector<Handle(AIS_InteractiveObject)> m_preview;
    std::map<const AIS_InteractiveObject*, sketch::Id> m_constraintObjects;
    ViewItems m_viewItems;
    std::vector<render::SketchStroke> m_viewPreview;
    std::vector<render::MarkerItem> m_viewPreviewMarkers;
};

} // namespace kalve137::app
