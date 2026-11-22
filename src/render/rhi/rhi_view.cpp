// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_view.h"

#include "log/log.h"
#include "render/draw_list.h"
#include "render/rhi/rhi_backend.h"

#include <QColor>
#include <QElapsedTimer>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QOffscreenSurface>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtGui/qtguiglobal.h>
#include <rhi/qrhi.h>
#if QT_CONFIG(opengl)
#include <QOpenGLContext>
#include <QSurfaceFormat>
#endif
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#define KALVE137_RHI_VULKAN
#include <QVulkanInstance>
#endif

#include <cmath>
#include <memory>
#include <string_view>

namespace kalve137::render {

namespace {

using Api = QRhiWidget::Api;

constexpr double kWheelNotch = 120.0;
constexpr int kSamples = 4;

Api widgetApi(GraphicsApi api)
{
    switch (api) {
    case GraphicsApi::Direct3D11:
        return Api::Direct3D11;
    case GraphicsApi::Direct3D12:
        return Api::Direct3D12;
    case GraphicsApi::Metal:
        return Api::Metal;
    case GraphicsApi::OpenGL:
        return Api::OpenGL;
    case GraphicsApi::Vulkan:
        return Api::Vulkan;
    }
    return Api::Null;
}

QString apiName(GraphicsApi api)
{
    const std::string_view name = graphicsApiName(api);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

DeviceProbe probeWith(QRhi::Implementation implementation, QRhiInitParams* params)
{
    const std::unique_ptr<QRhi> rhi(QRhi::create(implementation, params));
    DeviceProbe result;
    if (rhi == nullptr) {
        return result;
    }
    result.created = true;
    result.texelFetch = rhi->isFeatureSupported(QRhi::TexelFetch);
#if QT_CONFIG(opengl)
    if (implementation == QRhi::OpenGLES2) {
        const auto* handles = static_cast<const QRhiGles2NativeHandles*>(rhi->nativeHandles());
        if (handles != nullptr && handles->context != nullptr) {
            const QSurfaceFormat format = handles->context->format();
            result.openGl = true;
            result.openGlEs = handles->context->isOpenGLES();
            result.major = format.majorVersion();
            result.minor = format.minorVersion();
        }
    }
#endif
    return result;
}

DeviceProbe probe(GraphicsApi api)
{
    switch (api) {
    case GraphicsApi::Direct3D11:
#if defined(Q_OS_WIN)
    {
        QRhiD3D11InitParams params;
        return probeWith(QRhi::D3D11, &params);
    }
#else
        return {};
#endif
    case GraphicsApi::Direct3D12:
#if defined(Q_OS_WIN)
    {
        QRhiD3D12InitParams params;
        return probeWith(QRhi::D3D12, &params);
    }
#else
        return {};
#endif
    case GraphicsApi::Metal:
#if QT_CONFIG(metal)
    {
        QRhiMetalInitParams params;
        return probeWith(QRhi::Metal, &params);
    }
#else
        return {};
#endif
    case GraphicsApi::OpenGL:
#if QT_CONFIG(opengl)
    {
        const std::unique_ptr<QOffscreenSurface> surface(QRhiGles2InitParams::newFallbackSurface());
        QRhiGles2InitParams params;
        params.fallbackSurface = surface.get();
        return probeWith(QRhi::OpenGLES2, &params);
    }
#else
        return {};
#endif
    case GraphicsApi::Vulkan:
#if defined(KALVE137_RHI_VULKAN)
    {
        QVulkanInstance instance;
        if (!instance.create()) {
            return {};
        }
        QRhiVulkanInitParams params;
        params.inst = &instance;
        return probeWith(QRhi::Vulkan, &params);
    }
#else
        return {};
#endif
    }
    return {};
}

void styleNotice(QLabel& notice, GraphicsApi api, StartProblem problem)
{
    notice.setObjectName(QStringLiteral("viewFailure"));
    notice.setAlignment(Qt::AlignCenter);
    notice.setWordWrap(true);
    notice.setAutoFillBackground(true);
    const QString oldView = QLabel::tr("Start Kalve without --view new to use the old view.");
    if (problem == StartProblem::TooOld && api == GraphicsApi::OpenGL) {
        notice.setText(QLabel::tr("The new view needs OpenGL 3.2 or OpenGL ES 3.0.\n%1").arg(oldView));
    } else if (problem == StartProblem::TooOld) {
        notice.setText(
            QLabel::tr("The graphics device is too old for the new view with %1.\n%2").arg(apiName(api), oldView));
    } else {
        notice.setText(QLabel::tr("The graphics device couldn't start with %1.\n%2").arg(apiName(api), oldView));
    }
}

} // namespace

StartProblem startProblem(const DeviceProbe& probe)
{
    if (!probe.created) {
        return StartProblem::NoDevice;
    }
    if (!probe.texelFetch) {
        return StartProblem::TooOld;
    }
    if (probe.openGl) {
        const bool recent =
            probe.openGlEs ? probe.major >= 3 : probe.major > 3 || (probe.major == 3 && probe.minor >= 2);
        if (!recent) {
            return StartProblem::TooOld;
        }
    }
    return StartProblem::None;
}

StartProblem checkStart(GraphicsApi api)
{
    const DeviceProbe found = probe(api);
    const StartProblem problem = startProblem(found);
    switch (problem) {
    case StartProblem::None:
        break;
    case StartProblem::NoDevice:
        K137_ERROR(Viewer, "The new view could not start its graphics device with {}", graphicsApiName(api));
        break;
    case StartProblem::TooOld:
        K137_ERROR(Viewer, "The new view needs OpenGL 3.2 or OpenGL ES 3.0 and {} offers {}.{}{}", graphicsApiName(api),
                   found.major, found.minor, found.openGlEs ? " ES" : "");
        break;
    }
    return problem;
}

bool canStart(GraphicsApi api)
{
    return checkStart(api) == StartProblem::None;
}

QLabel* makeFailureNotice(GraphicsApi api, StartProblem problem, QWidget* parent)
{
    auto* notice = new QLabel(parent);
    styleNotice(*notice, api, problem);
    return notice;
}

RhiView::RhiView(GraphicsApi api, QWidget* parent) : QRhiWidget(parent), m_api(api)
{
    setApi(widgetApi(api));
    setSampleCount(kSamples);
    m_message = new QLabel(this);
    styleNotice(*m_message, api, StartProblem::NoDevice);
    m_message->hide();
    connect(this, &QRhiWidget::renderFailed, this, &RhiView::fail);
}

RhiView::~RhiView() = default;

void RhiView::setViewportStyle(const ViewportStyle& style)
{
    if (style == m_style) {
        return;
    }
    m_style = style;
    updateChunks();
}

void RhiView::setScene(snapshot::Scene scene)
{
    m_scene = std::move(scene);
    updateChunks();
}

void RhiView::updateChunks()
{
    QElapsedTimer timer;
    timer.start();
    m_chunks.update(m_scene, m_style);
    m_updateMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    update();
}

std::optional<double> RhiView::benchFrame()
{
    QRhiTexture* resolved = resolvedTexture();
    if (!m_ready || m_failed || rhi() == nullptr || resolved == nullptr) {
        return std::nullopt;
    }
    QElapsedTimer timer;
    timer.start();
    QRhiCommandBuffer* buffer = nullptr;
    if (rhi()->beginOffscreenFrame(&buffer) != QRhi::FrameOpSuccess) {
        return std::nullopt;
    }
    record(buffer, renderTarget());
    QRhiReadbackResult result;
    QRhiReadbackDescription description(resolved);
    description.setRect(QRect(0, 0, 1, 1));
    QRhiResourceUpdateBatch* batch = rhi()->nextResourceUpdateBatch();
    batch->readBackTexture(description, &result);
    buffer->resourceUpdate(batch);
    if (rhi()->endOffscreenFrame() != QRhi::FrameOpSuccess) {
        return std::nullopt;
    }
    return static_cast<double>(timer.nsecsElapsed()) / 1e6;
}

void RhiView::initialize(QRhiCommandBuffer*)
{
    if (m_backend.isEmpty()) {
        m_backend = QString::fromLatin1(rhi()->backendName());
        m_device = QString::fromUtf8(rhi()->driverInfo().deviceName);
        K137_INFO(Viewer, "New view draws with {} on {} at {}x samples", m_backend.toStdString(),
                  m_device.toStdString(), sampleCount());
    }
    if (m_bodies == nullptr) {
        m_bodies = std::make_unique<RhiBackend>(rhi());
    }
    if (!m_bodies->setTarget(renderTarget()->renderPassDescriptor(), sampleCount())) {
        fail();
    }
}

void RhiView::render(QRhiCommandBuffer* buffer)
{
    record(buffer, renderTarget());
    if (!m_ready) {
        m_ready = true;
        QMetaObject::invokeMethod(this, &RhiView::ready, Qt::QueuedConnection);
    }
}

void RhiView::releaseResources()
{
    m_bodies.reset();
}

void RhiView::setCamera(const camera::Camera& camera)
{
    m_camera = camera;
    update();
}

camera::Viewport RhiView::viewport() const
{
    const double ratio = devicePixelRatioF();
    return {static_cast<int>(std::lround(width() * ratio)), static_cast<int>(std::lround(height() * ratio)), ratio};
}

void RhiView::resizeEvent(QResizeEvent* event)
{
    QRhiWidget::resizeEvent(event);
    placeMessage();
    emit resized();
}

void RhiView::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    emit mousePressed(event->position(), event->button(), event->buttons(), event->modifiers());
}

void RhiView::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
    emit mouseReleased(event->position(), event->button(), event->buttons(), event->modifiers());
}

void RhiView::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
    emit mouseMoved(event->position(), event->buttons(), event->modifiers());
}

void RhiView::wheelEvent(QWheelEvent* event)
{
    event->accept();
    emit wheelTurned(event->position(), event->angleDelta().y() / kWheelNotch);
}

void RhiView::record(QRhiCommandBuffer* buffer, QRhiRenderTarget* target)
{
    const snapshot::Color& background = m_style.background;
    const QColor clear = QColor::fromRgbF(background.red, background.green, background.blue, background.alpha);
    QRhiResourceUpdateBatch* updates = nullptr;
    if (!m_failed && m_bodies != nullptr) {
        updates = rhi()->nextResourceUpdateBatch();
        const DrawList list = m_chunks.draw(makeFrame(m_camera, viewport(), m_scene.origin, background));
        if (!m_bodies->prepare(list, updates)) {
            fail();
        }
    }
    buffer->beginPass(target, clear, {1.0F, 0}, updates);
    if (!m_failed && m_bodies != nullptr) {
        m_bodies->draw(buffer, target->pixelSize());
    }
    buffer->endPass();
}

QRhiTexture* RhiView::resolvedTexture() const
{
    if (colorTexture() != nullptr) {
        return colorTexture();
    }
    const QRhiRenderTarget* target = renderTarget();
    if (target == nullptr || target->resourceType() != QRhiResource::TextureRenderTarget) {
        return nullptr;
    }
    const QRhiTextureRenderTargetDescription description =
        static_cast<const QRhiTextureRenderTarget*>(target)->description();
    return description.colorAttachmentCount() > 0 ? description.cbeginColorAttachments()->resolveTexture() : nullptr;
}

void RhiView::fail()
{
    if (m_failed) {
        return;
    }
    m_failed = true;
    K137_ERROR(Viewer, "The new view lost its graphics device with {}", graphicsApiName(m_api));
    placeMessage();
    m_message->show();
}

void RhiView::placeMessage()
{
    m_message->setGeometry(rect());
}

} // namespace kalve137::render
