// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/viewport.h"

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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kalve137::render {

void Viewport::record(QRhiCommandBuffer* buffer, QRhiRenderTarget* target)
{
    const snapshot::Color& background = m_style.background;
    const QColor clear = QColor::fromRgbF(background.red, background.green, background.blue, background.alpha);
    QRhiResourceUpdateBatch* updates = nullptr;
    if (!m_failed && m_bodies != nullptr) {
        updates = rhi()->nextResourceUpdateBatch();
        const camera::Viewport port = viewport();
        m_grid.update(m_camera, port, m_scene.origin);
        refreshDatums();
        DrawList list = m_chunks.draw(frame());
        list.grid = m_grid.draw(m_camera, port);
        list.datums = datumDraw();
        list.markers = markerDraw();
        list.sketches = sketchDraw();
        list.activeSketch = activeSketchDraw();
        list.annotations = annotationDraw();
        if (!m_bodies->prepare(list, updates)) {
            fail();
        }
    }
    buffer->beginPass(target, clear, {1.0F, 0}, updates);
    if (!m_failed && m_bodies != nullptr) {
        m_bodies->draw(buffer, target->pixelSize());
    }
    buffer->endPass();
    if (!m_failed && m_bodies != nullptr) {
        m_bodies->drawPost(buffer);
    }
}

QRhiTexture* Viewport::resolvedTexture() const
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

void Viewport::fail()
{
    if (m_failed) {
        return;
    }
    m_failed = true;
    K137_ERROR(Viewer, "The viewport lost its graphics device with {}", graphicsApiName(m_api));
    placeMessage();
    m_message->show();
}

void Viewport::placeMessage()
{
    m_message->setGeometry(rect());
}

} // namespace kalve137::render
