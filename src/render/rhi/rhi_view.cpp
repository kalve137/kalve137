// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_view.h"

#include "log/log.h"

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

bool probe(GraphicsApi api)
{
    switch (api) {
    case GraphicsApi::Direct3D11:
#if defined(Q_OS_WIN)
    {
        QRhiD3D11InitParams params;
        return QRhi::probe(QRhi::D3D11, &params);
    }
#else
        return false;
#endif
    case GraphicsApi::Direct3D12:
#if defined(Q_OS_WIN)
    {
        QRhiD3D12InitParams params;
        return QRhi::probe(QRhi::D3D12, &params);
    }
#else
        return false;
#endif
    case GraphicsApi::Metal:
#if QT_CONFIG(metal)
    {
        QRhiMetalInitParams params;
        return QRhi::probe(QRhi::Metal, &params);
    }
#else
        return false;
#endif
    case GraphicsApi::OpenGL:
#if QT_CONFIG(opengl)
    {
        const std::unique_ptr<QOffscreenSurface> surface(QRhiGles2InitParams::newFallbackSurface());
        QRhiGles2InitParams params;
        params.fallbackSurface = surface.get();
        return QRhi::probe(QRhi::OpenGLES2, &params);
    }
#else
        return false;
#endif
    case GraphicsApi::Vulkan:
#if defined(KALVE137_RHI_VULKAN)
    {
        QVulkanInstance instance;
        if (!instance.create()) {
            return false;
        }
        QRhiVulkanInitParams params;
        params.inst = &instance;
        return QRhi::probe(QRhi::Vulkan, &params);
    }
#else
        return false;
#endif
    }
    return false;
}

void styleNotice(QLabel& notice, GraphicsApi api)
{
    notice.setObjectName(QStringLiteral("viewFailure"));
    notice.setAlignment(Qt::AlignCenter);
    notice.setWordWrap(true);
    notice.setAutoFillBackground(true);
    notice.setText(QLabel::tr("The graphics device couldn't start with %1.\nStart Kalve without --view new to use the "
                              "old view.")
                       .arg(apiName(api)));
}

} // namespace

bool canStart(GraphicsApi api)
{
    if (probe(api)) {
        return true;
    }
    K137_ERROR(Viewer, "The new view could not start its graphics device with {}", graphicsApiName(api));
    return false;
}

QLabel* makeFailureNotice(GraphicsApi api, QWidget* parent)
{
    auto* notice = new QLabel(parent);
    styleNotice(*notice, api);
    return notice;
}

RhiView::RhiView(GraphicsApi api, QWidget* parent) : QRhiWidget(parent), m_api(api)
{
    setApi(widgetApi(api));
    m_message = new QLabel(this);
    styleNotice(*m_message, api);
    m_message->hide();
    connect(this, &QRhiWidget::renderFailed, this, &RhiView::fail);
}

void RhiView::setViewportStyle(const ViewportStyle& style)
{
    if (style == m_style) {
        return;
    }
    m_style = style;
    update();
}

void RhiView::setScene(snapshot::Scene scene)
{
    m_scene = std::move(scene);
}

std::optional<double> RhiView::benchFrame()
{
    if (!m_ready || m_failed || rhi() == nullptr || renderTarget() == nullptr || colorTexture() == nullptr) {
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
    QRhiReadbackDescription description(colorTexture());
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
        K137_INFO(Viewer, "New view draws with {} on {}", m_backend.toStdString(), m_device.toStdString());
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

void RhiView::releaseResources() {}

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
    const Color& background = m_style.background;
    const QColor clear = QColor::fromRgbF(background.red, background.green, background.blue, background.alpha);
    buffer->beginPass(target, clear, {1.0F, 0});
    buffer->endPass();
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
