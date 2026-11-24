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

constexpr double kWheelNotch = 120.0;

void Viewport::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    if (m_leftCaptured && event->button() == Qt::LeftButton) {
        emit capturedPress(event->position(), event->modifiers());
        return;
    }
    emit mousePressed(event->position(), event->button(), event->buttons(), event->modifiers());
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
    if (m_leftCaptured && event->button() == Qt::LeftButton) {
        emit capturedRelease(event->position());
        return;
    }
    emit mouseReleased(event->position(), event->button(), event->buttons(), event->modifiers());
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_leftCaptured && event->button() == Qt::LeftButton) {
        event->accept();
        emit capturedDoubleClick(event->position());
        return;
    }
    QRhiWidget::mouseDoubleClickEvent(event);
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
    emit mouseMoved(event->position(), event->buttons(), event->modifiers());
}

void Viewport::wheelEvent(QWheelEvent* event)
{
    event->accept();
    emit wheelTurned(event->position(), event->angleDelta().y() / kWheelNotch);
}

void Viewport::leaveEvent(QEvent* event)
{
    QRhiWidget::leaveEvent(event);
    emit mouseLeft();
}

} // namespace kalve137::render
