// SPDX-FileCopyrightText: 2023 Kirill Gavrilov
// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later AND MIT

#pragma once

#include <Standard_WarningsDisable.hxx>
#include <QAbstractNativeEventFilter>
#include <QColor>
#include <QOpenGLWidget>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <Standard_WarningsRestore.hxx>

#include "viewer/box_band.h"
#include "viewer/grid.h"
#include "viewer/navigation.h"
#include "viewer/view_turn.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewController.hxx>
#include <V3d_View.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <functional>
#include <optional>
#include <vector>

class AIS_ViewCube;

namespace kalve137::viewer {

struct ViewCubeStyle {
    QColor face;
    QColor edge;
    QColor label;
    QColor hover;
    QColor axisX;
    QColor axisY;
    QColor axisZ;
    QString labelFont;
};

class OcctViewWidget : public QOpenGLWidget, public QAbstractNativeEventFilter, public AIS_ViewController {
    Q_OBJECT

public:
    explicit OcctViewWidget(QWidget* parent = nullptr);
    ~OcctViewWidget() override;

    void fitAll();
    void fitAround(double halfSize);
    void homeView();
    void standardView(StandardView view);
    void setPerspective(bool perspective);
    bool isPerspective() const;
    void setNavigation(const Navigation& navigation);
    const Navigation& navigation() const { return m_navigation; }
    void setBackgroundColor(const QColor& color);
    void setHighlightColors(const QColor& hover, const QColor& selected);
    void setViewCubeStyle(const ViewCubeStyle& style);
    void setGridStyle(const GridStyle& style);
    void setGridVisible(bool visible);
    void setGridLabels(bool labels);
    void setGridSpacing(bool adaptive, double spacing);
    double gridStep() const;
    void lookAt(const gp_Ax3& plane);
    bool facesStandardView() const;
    void turnView(ViewTurn way);
    void animateCamera(const std::function<void()>& change);
    QRectF viewCubeRect() const;
    void setGridPlane(const gp_Ax3& plane);
    const gp_Ax3& gridPlane() const { return m_grid->plane(); }
    void setGridExtent(const std::optional<GridExtent>& extent);
    void redraw();

    void setLeftButtonCaptured(bool captured);
    bool isLeftButtonCaptured() const { return m_leftCaptured; }

    using BoxFilter = std::function<std::vector<bool>(const std::vector<Handle(SelectMgr_EntityOwner)>&)>;
    void setBoxSelection(BoxFilter filter);
    bool selectedByBox() const { return m_selectedByBox; }
    void showBand(QPointF from, QPointF to);
    void hideBand();

    std::optional<gp_Pnt> pickOnPlane(const QPointF& pos, const gp_Ax3& plane) const;
    void detectAt(const QPointF& pos);
    double worldPerPixel() const;
    QPointF toScreen(const gp_Pnt& point) const;
    gp_Dir towardViewer(const gp_Pnt& point) const;

    bool isReady() const { return !m_view.IsNull() && !m_view->Window().IsNull(); }
    bool isAnimating() const;
    const Handle(AIS_InteractiveContext)& aisContext() const { return m_context; }
    const QString& glInfo() const { return m_glInfo; }

    QSize minimumSizeHint() const override { return {200, 200}; }
    QSize sizeHint() const override { return {1280, 800}; }

    void OnSubviewChanged(const Handle(AIS_InteractiveContext)& context, const Handle(V3d_View)& oldView,
                          const Handle(V3d_View)& newView) override;
    void OnSelectionChanged(const Handle(AIS_InteractiveContext)& context, const Handle(V3d_View)& view) override;

signals:
    void ready();
    void selectionChanged();
    void hoverChanged();
    void capturedPress(QPointF pos, Qt::KeyboardModifiers modifiers);
    void capturedRelease(QPointF pos);
    void capturedDoubleClick(QPointF pos);
    void pointerMoved(QPointF pos, Qt::MouseButtons buttons);
    void menuRequested(QPoint globalPos);
    void rendered();
    void frameFailed(bool redrawStopped);

protected:
    void initializeGL() override;
    void paintGL() override;

    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    const Handle(Graphic3d_Camera)& targetCamera() const;
    void startCameraAnimation(const Handle(Graphic3d_Camera)& start, const Handle(Graphic3d_Camera)& end);
    void updateView();
    bool isViewCubeHovered() const;
    void updateGlInfo();
    void scaleViewCube();
    void traceInput(const char* what, const QMouseEvent* event) const;
    void updateGrid(bool force);
    void failFrame(V3d_View& view, const char* reason);
    bool recoverCamera(V3d_View& view);
    void logFailedFrame(const V3d_View& view, const char* reason) const;
    bool startBox(const QMouseEvent* event);
    bool dragBox(const QMouseEvent* event);
    bool finishBox(const QMouseEvent* event);
    void handleViewRedraw(const Handle(AIS_InteractiveContext)& context, const Handle(V3d_View)& view) override;
    Handle(V3d_Viewer) m_viewer;
    Handle(V3d_View) m_view;
    Handle(V3d_View) m_focusView;
    Handle(AIS_InteractiveContext) m_context;
    Handle(AIS_ViewCube) m_viewCube;
    Handle(GridObject) m_grid;
    Handle(GridObject) m_gridWash;
    std::optional<GridExtent> m_gridExtent;
    Handle(SelectMgr_EntityOwner) m_hovered;
    Handle(Graphic3d_Camera) m_lastGoodCamera = new Graphic3d_Camera();
    bool m_hasGoodCamera = false;
    int m_failedFrames = 0;

    QString m_glInfo;
    Navigation m_navigation;
    bool m_gridVisible = true;
    bool m_gridAdaptive = true;
    double m_gridSpacing = 10.0;
    bool m_hasTouchInput = false;
    bool m_leftCaptured = false;
    bool m_leftOnViewCube = false;

    BoxBand m_band;
    BoxFilter m_boxFilter;
    std::optional<QPointF> m_boxStart;
    Qt::KeyboardModifiers m_boxModifiers;
    bool m_boxing = false;
    bool m_selectedByBox = false;
};

} // namespace kalve137::viewer
