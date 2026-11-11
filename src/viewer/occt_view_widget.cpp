// SPDX-FileCopyrightText: 2023 Kirill Gavrilov
// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later AND MIT

#include "viewer/occt_view_widget.h"

#include "viewer/camera_guard.h"
#include "viewer/style.h"
#include "viewer/view_turn.h"

#include "log/log.h"

#include <OcctGlTools.h>
#include <OcctQtTools.h>

#include <Standard_WarningsDisable.hxx>
#include <QApplication>
#include <QMessageBox>
#include <QMouseEvent>
#include <Standard_WarningsRestore.hxx>

#include <AIS_AnimationCamera.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <Message.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_Vec2.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Prs3d_TextAspect.hxx>
#include <TCollection_AsciiString.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <gp_Dir.hxx>

#include <cmath>
#include <exception>
#include <format>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

namespace kalve137::viewer {

namespace {

using Vec2i = NCollection_Vec2<int>;
using Vec2d = NCollection_Vec2<double>;

constexpr double kFitMargin = 0.3;

constexpr double kPickTolerancePixels = 4.0;

constexpr double kRecoveryHalfSize = 100.0;
constexpr int kMaxFailedFrames = 3;

constexpr double kViewCubeSize = 64.0;
constexpr int kViewCubeOffsetX = 100;
constexpr int kViewCubeOffsetY = 150;
constexpr double kViewCubeRoundness = 0.12;
constexpr double kViewCubeFacetFraction = 0.12;
constexpr double kViewCubeAxesPadding = 6.0;
constexpr double kViewCubeAxisRadius = 1.4;
constexpr double kViewCubeAxisConeRadius = 3.2;
constexpr double kViewCubeFontHeight = 13.0;
constexpr double kViewCubeAxisFontHeight = 15.0;
constexpr double kViewTransitionSeconds = 0.5;
constexpr double kInstantTransitionSeconds = 0.001;
constexpr float kHoverTransparency = static_cast<float>(style::kTintStrong);
constexpr double kViewCubeHoverTransparency = style::kTintFaint;

template <typename Drawable = Aspect_Drawable>
Drawable toDrawable(WId id)
{
    if constexpr (std::is_pointer_v<Drawable>) {
        return reinterpret_cast<Drawable>(id); // NOLINT(performance-no-int-to-ptr): WId holds an HWND.
    } else {
        return static_cast<Drawable>(id);
    }
}

} // namespace

OcctViewWidget::OcctViewWidget(QWidget* parent) : QOpenGLWidget(parent)
{
    Handle(Aspect_DisplayConnection) display;
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(HAVE_WAYLAND)
    display = new Xw_DisplayConnection();
#endif
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display, false);
    driver->ChangeOptions().buffersNoSwap = true;
    driver->ChangeOptions().buffersOpaqueAlpha = true;
    driver->ChangeOptions().useSystemBuffer = false;

    m_viewer = new V3d_Viewer(driver);
    Handle(V3d_AmbientLight) ambient = new V3d_AmbientLight();
    ambient->SetIntensity(0.35f);
    Handle(V3d_DirectionalLight) keyLight =
        new V3d_DirectionalLight(gp_Dir(-0.35, -0.6, -1.0), Quantity_NOC_WHITE, true);
    m_viewer->AddLight(ambient);
    m_viewer->AddLight(keyLight);
    m_viewer->SetLightOn();

    m_context = new AIS_InteractiveContext(m_viewer);
    m_grid = new GridObject(GridObject::Part::Lines);
    m_gridWash = new GridObject(GridObject::Part::Wash);
    const auto addGridLayer = [this](const char* name, Graphic3d_ZLayerId after) {
        Graphic3d_ZLayerSettings settings;
        settings.SetName(name);
        settings.SetClearDepth(false);
        settings.SetEnableDepthTest(true);
        settings.SetEnableDepthWrite(false);
        Graphic3d_ZLayerId id = Graphic3d_ZLayerId_UNKNOWN;
        return m_viewer->InsertLayerAfter(id, settings, after) ? id : Graphic3d_ZLayerId_UNKNOWN;
    };
    const Graphic3d_ZLayerId washLayer = addGridLayer("GridWash", Graphic3d_ZLayerId_Default);
    const Graphic3d_ZLayerId gridLayer =
        washLayer != Graphic3d_ZLayerId_UNKNOWN ? addGridLayer("Grid", washLayer) : Graphic3d_ZLayerId_UNKNOWN;
    if (gridLayer != Graphic3d_ZLayerId_UNKNOWN) {
        m_gridWash->SetZLayer(washLayer);
        m_grid->SetZLayer(gridLayer);
    } else {
        K137_WARN(Viewer, "No grid layer, the grid draws under everything");
        m_gridWash->SetZLayer(Graphic3d_ZLayerId_BotOSD);
        m_grid->SetZLayer(Graphic3d_ZLayerId_BotOSD);
    }

    m_viewCube = new AIS_ViewCube();
    m_viewCube->SetViewAnimation(myViewAnimation);
    m_viewCube->SetFixedAnimationLoop(false);
    m_viewCube->SetAutoStartAnimation(true);
    m_viewCube->SetRoundRadius(kViewCubeRoundness);
    scaleViewCube();

    setNavigation(Navigation{});
    SetShowRotateCenter(false);
    SetShowPanAnchorPoint(false);

    m_view = m_viewer->CreateView();
    m_view->SetImmediateUpdate(false);
#ifndef __APPLE__
    m_view->ChangeRenderingParams().NbMsaaSamples = 4;
#endif

    setAttribute(Qt::WA_AcceptTouchEvents);
    setMouseTracking(true);
    setBackgroundRole(QPalette::NoRole);
    setFocusPolicy(Qt::StrongFocus);
    setUpdatesEnabled(true);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

OcctViewWidget::~OcctViewWidget()
{
    Handle(Aspect_DisplayConnection) display = m_viewer->Driver()->GetDisplayConnection();

    m_context->RemoveAll(false);
    m_context.Nullify();
    m_view->Remove();
    m_view.Nullify();
    m_viewer.Nullify();

    makeCurrent();
    display.Nullify();
}

void OcctViewWidget::fitAll()
{
    m_view->FitAll(kFitMargin, false);
    m_view->Invalidate();
    updateView();
}

void OcctViewWidget::fitAround(double halfSize)
{
    Bnd_Box box;
    box.Update(-halfSize, -halfSize, -halfSize, halfSize, halfSize, halfSize);
    m_view->FitAll(box, kFitMargin, false);
    m_view->Invalidate();
    updateView();
}

void OcctViewWidget::homeView()
{
    m_view->SetProj(V3d_TypeOfOrientation_Zup_AxoRight, false);
    m_view->Invalidate();
    updateView();
}

void OcctViewWidget::standardView(StandardView view)
{
    const V3d_TypeOfOrientation orientation = [view] {
        switch (view) {
        case StandardView::Front:
            return V3d_TypeOfOrientation_Zup_Front;
        case StandardView::Back:
            return V3d_TypeOfOrientation_Zup_Back;
        case StandardView::Left:
            return V3d_TypeOfOrientation_Zup_Left;
        case StandardView::Right:
            return V3d_TypeOfOrientation_Zup_Right;
        case StandardView::Top:
            return V3d_TypeOfOrientation_Zup_Top;
        case StandardView::Bottom:
            return V3d_TypeOfOrientation_Zup_Bottom;
        }
        return V3d_TypeOfOrientation_Zup_AxoRight;
    }();
    m_view->SetProj(orientation, false);
    m_view->Invalidate();
    updateView();
}

void OcctViewWidget::setPerspective(bool perspective)
{
    m_view->Camera()->SetProjectionType(perspective ? Graphic3d_Camera::Projection_Perspective
                                                    : Graphic3d_Camera::Projection_Orthographic);
    m_view->Invalidate();
    updateView();
}

bool OcctViewWidget::isPerspective() const
{
    return m_view->Camera()->ProjectionType() == Graphic3d_Camera::Projection_Perspective;
}

void OcctViewWidget::setNavigation(const Navigation& navigation)
{
    m_navigation = navigation;
    const unsigned int middle = Aspect_VKeyMouse_MiddleButton;
    const unsigned int ctrl = Aspect_VKeyFlags_CTRL;
    const unsigned int shift = Aspect_VKeyFlags_SHIFT;
    NCollection_DataMap<unsigned int, AIS_MouseGesture>& gestures = ChangeMouseGestureMap();
    gestures.Clear();
    myMouseGestureMapDrag.Clear();
    SetAllowDragging(false);
    switch (navigation.preset) {
    case NavigationPreset::MiddleOrbitsCtrlPans:
        gestures.Bind(middle, AIS_MouseGesture_RotateOrbit);
        gestures.Bind(middle | ctrl, AIS_MouseGesture_Pan);
        gestures.Bind(middle | shift, AIS_MouseGesture_Zoom);
        break;
    case NavigationPreset::MiddlePansShiftOrbits:
        gestures.Bind(middle, AIS_MouseGesture_Pan);
        gestures.Bind(middle | shift, AIS_MouseGesture_RotateOrbit);
        gestures.Bind(middle | ctrl, AIS_MouseGesture_Zoom);
        break;
    case NavigationPreset::MiddleOrbitsShiftPans:
        gestures.Bind(middle, AIS_MouseGesture_RotateOrbit);
        gestures.Bind(middle | shift, AIS_MouseGesture_Pan);
        gestures.Bind(middle | ctrl, AIS_MouseGesture_Zoom);
        break;
    }
    SetRotationMode(navigation.orbitAroundCursor ? AIS_RotationMode_PickLast : AIS_RotationMode_BndBoxActive);
    SetLockOrbitZUp(navigation.constrainedOrbit);
    m_viewCube->SetDuration(navigation.animateTransitions ? kViewTransitionSeconds : kInstantTransitionSeconds);
}

void OcctViewWidget::setBackgroundColor(const QColor& color)
{
    m_view->SetBackgroundColor(OcctQtTools::qtColorToOcct(color));
    updateView();
}

void OcctViewWidget::setHighlightColors(const QColor& hover, const QColor& selected)
{
    const Quantity_Color hoverColor = OcctQtTools::qtColorToOcct(hover);
    const Quantity_Color selectedColor = OcctQtTools::qtColorToOcct(selected);
    for (const Prs3d_TypeOfHighlight type : {Prs3d_TypeOfHighlight_Dynamic, Prs3d_TypeOfHighlight_LocalDynamic}) {
        m_context->HighlightStyle(type)->SetColor(hoverColor);
        m_context->HighlightStyle(type)->SetTransparency(kHoverTransparency);
    }
    m_context->HighlightStyle(Prs3d_TypeOfHighlight_Selected)->SetColor(selectedColor);
    m_context->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected)->SetColor(selectedColor);
    m_band.setColor(selectedColor);
    updateView();
}

void OcctViewWidget::setViewCubeStyle(const ViewCubeStyle& style)
{
    const auto occt = [](const QColor& color) { return OcctQtTools::qtColorToOcct(color); };
    m_viewCube->SetBoxColor(occt(style.face));
    m_viewCube->SetInnerColor(occt(style.edge));
    m_viewCube->BoxEdgeStyle()->SetColor(occt(style.edge));
    m_viewCube->BoxCornerStyle()->SetColor(occt(style.edge));
    m_viewCube->SetTextColor(occt(style.label));
    m_viewCube->SetFont(style.labelFont.toUtf8().constData());
    for (const Handle(Prs3d_ShadingAspect)& aspect :
         {m_viewCube->Attributes()->ShadingAspect(), m_viewCube->BoxEdgeStyle(), m_viewCube->BoxCornerStyle()}) {
        aspect->Aspect()->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    }
    const Handle(Prs3d_Drawer)& hover = m_viewCube->DynamicHilightAttributes();
    hover->SetColor(occt(style.hover));
    if (hover->HasOwnShadingAspect()) {
        const Handle(Prs3d_ShadingAspect)& tint = hover->ShadingAspect();
        tint->SetColor(occt(style.hover));
        tint->SetTransparency(kViewCubeHoverTransparency);
        tint->Aspect()->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    }
    const std::pair<Prs3d_DatumParts, QColor> axes[] = {{Prs3d_DatumParts_XAxis, style.axisX},
                                                        {Prs3d_DatumParts_YAxis, style.axisY},
                                                        {Prs3d_DatumParts_ZAxis, style.axisZ}};
    if (!m_viewCube->Attributes()->HasOwnDatumAspect()) {
        m_viewCube->Attributes()->SetDatumAspect(new Prs3d_DatumAspect());
    }
    const Handle(Prs3d_DatumAspect)& datum = m_viewCube->Attributes()->DatumAspect();
    datum->SetDrawDatumAxes(Prs3d_DatumAxes_XYZAxes);
    m_viewCube->SetAxesLabels("X", "Y", "Z");
    for (const auto& [part, color] : axes) {
        datum->ShadingAspect(part)->SetColor(occt(color));
        const Handle(Prs3d_TextAspect)& text = datum->TextAspect(part);
        text->SetColor(occt(color));
        text->SetFont(style.labelFont.toUtf8().constData());
    }
    scaleViewCube();
    updateView();
}

void OcctViewWidget::scaleViewCube()
{
    const double ratio = devicePixelRatioF();
    m_viewCube->SetSize(kViewCubeSize * ratio);
    m_viewCube->SetBoxFacetExtension(kViewCubeSize * kViewCubeFacetFraction * ratio);
    m_viewCube->SetAxesPadding(kViewCubeAxesPadding * ratio);
    m_viewCube->SetAxesRadius(kViewCubeAxisRadius * ratio);
    m_viewCube->SetAxesConeRadius(kViewCubeAxisConeRadius * ratio);
    m_viewCube->SetAxesSphereRadius(kViewCubeAxisConeRadius * ratio);
    m_viewCube->SetFontHeight(kViewCubeFontHeight * ratio);
    if (m_viewCube->Attributes()->HasOwnDatumAspect()) {
        const Handle(Prs3d_DatumAspect)& datum = m_viewCube->Attributes()->DatumAspect();
        for (const Prs3d_DatumParts part : {Prs3d_DatumParts_XAxis, Prs3d_DatumParts_YAxis, Prs3d_DatumParts_ZAxis}) {
            datum->TextAspect(part)->SetHeight(kViewCubeAxisFontHeight * ratio);
        }
    }
    const Vec2i offset(static_cast<int>(std::lround(kViewCubeOffsetX * ratio)),
                       static_cast<int>(std::lround(kViewCubeOffsetY * ratio)));
    m_viewCube->SetTransformPersistence(new Graphic3d_TransformPers(
        static_cast<Graphic3d_TransModeFlags>(Graphic3d_TMF_TriedronPers | Graphic3d_TMF_OrthoPers),
        Aspect_TOTP_LEFT_LOWER, offset));
    if (m_context->IsDisplayed(m_viewCube)) {
        m_context->Redisplay(m_viewCube, false);
    }
}

void OcctViewWidget::lookAt(const gp_Ax3& plane)
{
    const Handle(Graphic3d_Camera)& camera = m_view->Camera();
    camera->SetDirection(plane.Direction().Reversed());
    camera->SetUp(plane.YDirection());
    m_view->Invalidate();
    updateView();
}

const Handle(Graphic3d_Camera)& OcctViewWidget::targetCamera() const
{
    if (!myViewAnimation->IsStopped() && !myViewAnimation->CameraEnd().IsNull()) {
        return myViewAnimation->CameraEnd();
    }
    return m_view->Camera();
}

bool OcctViewWidget::facesStandardView() const
{
    return isReady() && viewer::facesStandardView(*targetCamera());
}

void OcctViewWidget::turnView(ViewTurn way)
{
    if (!facesStandardView()) {
        return;
    }
    Handle(Graphic3d_Camera) start = new Graphic3d_Camera();
    start->Copy(m_view->Camera());
    Handle(Graphic3d_Camera) end = new Graphic3d_Camera();
    end->Copy(targetCamera());
    turn(*end, way);
    startCameraAnimation(start, end);
}

void OcctViewWidget::animateCamera(const std::function<void()>& change)
{
    if (!isReady() || !m_navigation.animateTransitions) {
        change();
        return;
    }
    Handle(Graphic3d_Camera) start = new Graphic3d_Camera();
    start->Copy(m_view->Camera());
    AbortViewAnimation();
    change();
    Handle(Graphic3d_Camera) end = new Graphic3d_Camera();
    end->Copy(m_view->Camera());
    if (!isUsable(*end)) {
        return;
    }
    m_view->Camera()->Copy(start);
    startCameraAnimation(start, end);
}

void OcctViewWidget::startCameraAnimation(const Handle(Graphic3d_Camera)& start, const Handle(Graphic3d_Camera)& end)
{
    myViewAnimation->Stop();
    myViewAnimation->SetView(m_view);
    myViewAnimation->SetCameraStart(start);
    myViewAnimation->SetCameraEnd(end);
    myViewAnimation->SetOwnDuration(m_viewCube->Duration());
    myViewAnimation->StartTimer(0.0, 1.0, true, false);
    m_view->Invalidate();
    updateView();
}

bool OcctViewWidget::isAnimating() const
{
    return !myViewAnimation->IsStopped();
}

QRectF OcctViewWidget::viewCubeRect() const
{
    const double half = kViewCubeSize / 2.0 + kViewCubeSize * kViewCubeFacetFraction;
    const QPointF center(kViewCubeOffsetX, height() - kViewCubeOffsetY);
    return {center - QPointF(half, half), QSizeF(2.0 * half, 2.0 * half)};
}

void OcctViewWidget::redraw()
{
    m_view->Invalidate();
    updateView();
}

void OcctViewWidget::setLeftButtonCaptured(bool captured)
{
    m_leftCaptured = captured;
}

std::optional<gp_Pnt> OcctViewWidget::pickOnPlane(const QPointF& pos, const gp_Ax3& plane) const
{
    if (!isReady()) {
        return std::nullopt;
    }
    const Vec2d point(pos.x(), pos.y());
    const Vec2i pixel(m_view->Window()->ConvertPointToBacking(point) + Vec2d(0.5));
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double dx = 0.0;
    double dy = 0.0;
    double dz = 0.0;
    m_view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);

    const gp_Pnt origin(x, y, z);
    const gp_Vec ray(dx, dy, dz);
    const gp_Vec normal(plane.Direction());
    const double along = ray.Dot(normal);
    if (std::abs(along) < 1e-9) {
        return std::nullopt;
    }
    const double t = gp_Vec(origin, plane.Location()).Dot(normal) / along;
    return origin.Translated(ray * t);
}

void OcctViewWidget::detectAt(const QPointF& pos)
{
    if (!isReady()) {
        return;
    }
    const Vec2i pixel(m_view->Window()->ConvertPointToBacking(Vec2d(pos.x(), pos.y())) + Vec2d(0.5));
    m_context->MoveTo(pixel.x(), pixel.y(), m_view, false);
}

double OcctViewWidget::worldPerPixel() const
{
    if (!isReady()) {
        return 1.0;
    }
    return m_view->Convert(1) * m_view->Window()->DevicePixelRatio();
}

QPointF OcctViewWidget::toScreen(const gp_Pnt& point) const
{
    if (!isReady()) {
        return {};
    }
    int x = 0;
    int y = 0;
    m_view->Convert(point.X(), point.Y(), point.Z(), x, y);
    const Vec2d logical = m_view->Window()->ConvertPointFromBacking(Vec2d(x, y));
    return {logical.x(), logical.y()};
}

void OcctViewWidget::failFrame(V3d_View& view, const char* reason)
{
    ResetViewInput();
    AbortViewAnimation();
    ++m_failedFrames;
    if (m_failedFrames == 1) {
        logFailedFrame(view, reason);
    }
    if (recoverCamera(view)) {
        view.Invalidate();
        updateView();
        emit frameFailed(false);
    } else if (m_failedFrames == kMaxFailedFrames) {
        emit frameFailed(true);
    }
}

bool OcctViewWidget::recoverCamera(V3d_View& view)
{
    try {
        if (m_failedFrames == 1 && m_hasGoodCamera) {
            view.Camera()->Copy(m_lastGoodCamera);
            return true;
        }
        if (m_failedFrames <= 2) {
            Bnd_Box box;
            box.Update(-kRecoveryHalfSize, -kRecoveryHalfSize, -kRecoveryHalfSize, kRecoveryHalfSize, kRecoveryHalfSize,
                       kRecoveryHalfSize);
            resetCamera(*view.Camera(), kRecoveryHalfSize);
            view.SetProj(V3d_TypeOfOrientation_Zup_AxoRight, false);
            view.FitAll(box, kFitMargin, false);
            K137_WARN(Viewer, "Camera reset to the home view after failed frames");
            return true;
        }
    } catch (const std::exception& failure) {
        K137_WARN(Viewer, "Camera recovery failed: {}", failure.what());
    }
    if (m_failedFrames == kMaxFailedFrames) {
        K137_ERROR(Viewer, "View stopped redrawing after {} failed frames, it retries on the next input",
                   kMaxFailedFrames);
    }
    return false;
}

void OcctViewWidget::logFailedFrame(const V3d_View& view, const char* reason) const
{
    const auto describe = [](const Bnd_Box& box) {
        if (box.IsVoid()) {
            return std::string("void");
        }
        const gp_Pnt low = box.CornerMin();
        const gp_Pnt high = box.CornerMax();
        return std::format("({:.6g}, {:.6g}, {:.6g}) to ({:.6g}, {:.6g}, {:.6g})", low.X(), low.Y(), low.Z(), high.X(),
                           high.Y(), high.Z());
    };
    const Graphic3d_Camera& camera = *view.Camera();
    const gp_Pnt eye = camera.Eye();
    const gp_Pnt center = camera.Center();
    const gp_Dir up = camera.Up();
    K137_WARN(Viewer, "Frame failed: {}", reason);
    K137_WARN(Viewer,
              "  Camera {} eye ({:.6g}, {:.6g}, {:.6g}) center ({:.6g}, {:.6g}, {:.6g}) up ({:.6g}, {:.6g}, {:.6g}) "
              "scale {:.6g} z {:.6g} to {:.6g}",
              camera.IsOrthographic() ? "orthographic" : "perspective", eye.X(), eye.Y(), eye.Z(), center.X(),
              center.Y(), center.Z(), up.X(), up.Y(), up.Z(), camera.Scale(), camera.ZNear(), camera.ZFar());
    K137_WARN(Viewer, "  Scene bounds {}, with zoom persistent objects {}", describe(view.View()->MinMaxValues(false)),
              describe(view.View()->MinMaxValues(true)));
}

void OcctViewWidget::OnSubviewChanged(const Handle(AIS_InteractiveContext)&, const Handle(V3d_View)&,
                                      const Handle(V3d_View)& newView)
{
    m_focusView = newView;
}

void OcctViewWidget::OnSelectionChanged(const Handle(AIS_InteractiveContext)&, const Handle(V3d_View)&)
{
    K137_DEBUG(Input, "Selection changed, {} selected", m_context->NbSelected());
    QMetaObject::invokeMethod(this, &OcctViewWidget::selectionChanged, Qt::QueuedConnection);
}

void OcctViewWidget::initializeGL()
{
    Handle(OpenGl_GraphicDriver) driver = Handle(OpenGl_GraphicDriver)::DownCast(m_viewer->Driver());
    OcctQtTools::qtGlCapsFromSurfaceFormat(driver->ChangeOptions(), format());

    const Aspect_Drawable nativeWindow = toDrawable(effectiveWinId());
    const Vec2i viewSize(rect().right() - rect().left(), rect().bottom() - rect().top());

    const bool isFirstInit = m_view->Window().IsNull();
    if (!OcctGlTools::InitializeGlWindow(m_view, nativeWindow, viewSize, devicePixelRatioF())) {
        K137_ERROR(Viewer, "OpenCascade could not wrap the OpenGL context");
        QMessageBox::critical(nullptr, tr("Kalve 137"), tr("OpenCascade could not wrap the OpenGL context."));
        QApplication::exit(1);
        return;
    }
    m_context->SetPixelTolerance(static_cast<int>(std::lround(kPickTolerancePixels * devicePixelRatioF())));
    scaleViewCube();

    makeCurrent();
    updateGlInfo();
    if (!isFirstInit) {
        return;
    }

    if (OcctQtTools::qtRegisterRawInput(nativeWindow)) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }

    m_context->Display(m_viewCube, 0, 0, false);
    if (m_gridVisible) {
        if (!m_gridExtent) {
            m_context->Display(m_gridWash, 0, -1, false);
        }
        m_context->Display(m_grid, 0, -1, false);
        updateGrid(true);
    }
    QMetaObject::invokeMethod(this, &OcctViewWidget::ready, Qt::QueuedConnection);
}

void OcctViewWidget::paintGL()
{
    if (m_view.IsNull() || m_view->Window().IsNull()) {
        return;
    }

    const double oldPixelRatio = m_view->Window()->DevicePixelRatio();
    if (m_view->Window()->NativeHandle() != OcctGlTools::GetGlNativeWindow(toDrawable(effectiveWinId()))) {
        Message::SendWarning() << "Native window handle has changed by QOpenGLWidget!";
        initializeGL();
    } else if (devicePixelRatioF() != oldPixelRatio) {
        initializeGL();
    }

    if (!OcctGlTools::InitializeGlFbo(m_view)) {
        K137_ERROR(Viewer, "Default framebuffer wrapper creation failed");
        QMessageBox::critical(nullptr, tr("Kalve 137"), tr("Default framebuffer wrapper creation failed."));
        QApplication::exit(1);
        return;
    }

    OcctGlTools::ResetGlStateBeforeOcct(m_view);

    Handle(V3d_View) view = !m_focusView.IsNull() ? m_focusView : m_view;
    view->InvalidateImmediate();
    try {
        repairUp(*view->Camera());
        AIS_ViewController::FlushViewEvents(m_context, view, true);
        if (isUsable(*view->Camera())) {
            m_lastGoodCamera->Copy(view->Camera());
            m_hasGoodCamera = true;
            m_failedFrames = 0;
        } else {
            failFrame(*view, "camera is not usable");
        }
    } catch (const std::exception& failure) {
        failFrame(*view, failure.what());
    }

    OcctGlTools::ResetGlStateAfterOcct(m_view);
    emit rendered();
}

bool OcctViewWidget::event(QEvent* event)
{
    if (m_view.IsNull()) {
        return QOpenGLWidget::event(event);
    }

    const QEvent::Type type = event->type();
    if (type == QEvent::TouchBegin || type == QEvent::TouchUpdate || type == QEvent::TouchEnd) {
        event->accept();
        m_hasTouchInput = true;
        if (OcctQtTools::qtHandleTouchEvent(*this, m_view, static_cast<QTouchEvent*>(event))) {
            updateView();
        }
        return true;
    }

    return QOpenGLWidget::event(event);
}

void OcctViewWidget::keyPressEvent(QKeyEvent* event)
{
    QOpenGLWidget::keyPressEvent(event);
}

void OcctViewWidget::traceInput([[maybe_unused]] const char* what, [[maybe_unused]] const QMouseEvent* event) const
{
    K137_TRACE(Input,
               "{} button={} modifiers={:#x} source={} at={},{} single={} pressed={} gesture={} touches={} "
               "touchSeen={} captured={} detected={} selected={}",
               what, event != nullptr ? static_cast<int>(event->button()) : -1,
               event != nullptr ? static_cast<unsigned>(event->modifiers().toInt()) : 0u,
               event != nullptr ? static_cast<int>(event->source()) : -1,
               event != nullptr ? event->position().x() : 0.0, event != nullptr ? event->position().y() : 0.0,
               myMouseSingleButton, static_cast<int>(myMousePressed), static_cast<int>(myMouseActiveGesture),
               HasTouchPoints(), m_hasTouchInput, m_leftCaptured, m_context->HasDetected(), m_context->NbSelected());
}

void OcctViewWidget::mousePressEvent(QMouseEvent* event)
{
    QOpenGLWidget::mousePressEvent(event);
    if (m_view.IsNull()) {
        return;
    }
    traceInput("press", event);
    if (m_hasTouchInput && event->source() == Qt::MouseEventSynthesizedBySystem) {
        return;
    }
    event->accept();
    if (m_leftCaptured && event->button() == Qt::LeftButton) {
        m_leftOnViewCube = isViewCubeHovered();
        if (!m_leftOnViewCube) {
            emit capturedPress(event->position(), event->modifiers());
            return;
        }
    }
    if (event->button() == Qt::RightButton) {
        emit menuRequested(event->globalPosition().toPoint());
        return;
    }
    startBox(event);
    if (OcctQtTools::qtHandleMouseEvent(*this, m_view, event)) {
        updateView();
    }
}

void OcctViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    QOpenGLWidget::mouseReleaseEvent(event);
    if (m_view.IsNull()) {
        return;
    }
    event->accept();
    traceInput("release", event);
    if (m_leftCaptured && event->button() == Qt::LeftButton && !std::exchange(m_leftOnViewCube, false)) {
        emit capturedRelease(event->position());
        return;
    }
    if (finishBox(event)) {
        return;
    }
    if (OcctQtTools::qtHandleMouseEvent(*this, m_view, event)) {
        updateView();
    }
    traceInput("handled release", event);
}

void OcctViewWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_leftCaptured && event->button() == Qt::LeftButton && !isViewCubeHovered()) {
        event->accept();
        emit capturedDoubleClick(event->position());
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void OcctViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    QOpenGLWidget::mouseMoveEvent(event);
    if (m_view.IsNull()) {
        return;
    }
    if (m_hasTouchInput && event->source() == Qt::MouseEventSynthesizedBySystem) {
        return;
    }
    event->accept();
    if (!dragBox(event) && OcctQtTools::qtHandleMouseEvent(*this, m_view, event)) {
        updateView();
    }
    emit pointerMoved(event->position(), event->buttons());
}

void OcctViewWidget::leaveEvent(QEvent* event)
{
    QOpenGLWidget::leaveEvent(event);
    if (!m_context.IsNull() && m_context->HasDetected()) {
        m_context->ClearDetected(false);
        updateView();
    }
}

void OcctViewWidget::wheelEvent(QWheelEvent* event)
{
    QOpenGLWidget::wheelEvent(event);
    if (m_view.IsNull() || m_view->Window().IsNull()) {
        return;
    }
    event->accept();

    const Vec2d point(event->position().x(), event->position().y());
    const Vec2i pixel(m_view->Window()->ConvertPointToBacking(point) + Vec2d(0.5));
    if (!m_view->Subviews().IsEmpty()) {
        Handle(V3d_View) pickedView = m_view->PickSubview(pixel);
        if (!pickedView.IsNull() && pickedView != m_focusView) {
            OnSubviewChanged(m_context, m_focusView, pickedView);
            updateView();
            return;
        }
    }

    const double notches = event->angleDelta().y() / 120.0 * (m_navigation.reverseZoom ? -1.0 : 1.0);
    const Aspect_ScrollDelta delta =
        m_navigation.zoomToCursor ? Aspect_ScrollDelta(pixel, notches) : Aspect_ScrollDelta(notches);
    if (UpdateMouseScroll(delta)) {
        updateView();
    }
}

bool OcctViewWidget::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
    if (OcctQtTools::qtHandleNativeEvent(*this, m_view, eventType, message)) {
        updateView();
        return true;
    }
    return false;
}

void OcctViewWidget::updateView()
{
    update();
}

bool OcctViewWidget::isViewCubeHovered() const
{
    return m_context->HasDetected() && !Handle(AIS_ViewCubeOwner)::DownCast(m_context->DetectedOwner()).IsNull();
}

void OcctViewWidget::updateGlInfo()
{
    using StringMap = NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString>;
    StringMap caps;
    m_view->DiagnosticInformation(caps, Graphic3d_DiagnosticInfo_Basic);
    TCollection_AsciiString info;
    for (StringMap::Iterator it(caps); it.More(); it.Next()) {
        if (it.Value().IsEmpty()) {
            continue;
        }
        if (!info.IsEmpty()) {
            info += "\n";
        }
        info += it.Key() + ": " + it.Value();
    }
    m_glInfo = QString::fromUtf8(info.ToCString());
}

void OcctViewWidget::handleViewRedraw(const Handle(AIS_InteractiveContext)& context, const Handle(V3d_View)& view)
{
    updateGrid(false);
    AIS_ViewController::handleViewRedraw(context, view);
    const Handle(SelectMgr_EntityOwner) hovered = m_context->HasDetected() ? m_context->DetectedOwner() : nullptr;
    if (hovered != m_hovered) {
        m_hovered = hovered;
        QMetaObject::invokeMethod(this, &OcctViewWidget::hoverChanged, Qt::QueuedConnection);
    }
    if (myToAskNextFrame) {
        updateView();
    }
}

} // namespace kalve137::viewer
