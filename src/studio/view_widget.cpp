// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "studio/view_widget.h"

#include "look/image.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace kalve137::studio {

ViewWidget::ViewWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumSize(320, 240);
}

void ViewWidget::setSnapshot(std::shared_ptr<const model::Snapshot> snapshot, bool stale)
{
    m_snapshot = std::move(snapshot);
    m_stale = stale;
    if (!m_fitted && m_snapshot && m_snapshot->built) {
        m_view = look::fitted(*m_snapshot, m_view);
        m_fitted = true;
    }
    m_hover = -1;
    invalidate();
}

void ViewWidget::setSelected(std::set<int> faces)
{
    m_selected = std::move(faces);
    invalidate();
}

void ViewWidget::fit()
{
    if (m_snapshot && m_snapshot->built) {
        m_view = look::fitted(*m_snapshot, m_view);
        invalidate();
    }
}

void ViewWidget::invalidate()
{
    m_dirty = true;
    update();
}

int ViewWidget::faceUnder(QPointF position) const
{
    const double ratio = devicePixelRatioF();
    return m_frame.faceAt(static_cast<int>(position.x() * ratio), static_cast<int>(position.y() * ratio));
}

void ViewWidget::paintEvent(QPaintEvent*)
{
    const double ratio = devicePixelRatioF();
    if (m_dirty || m_image.isNull()) {
        const auto started = std::chrono::steady_clock::now();
        look::Style style;
        style.hovered = m_hover;
        style.highlighted = m_selected;
        static const model::Snapshot empty;
        m_frame = look::render(m_snapshot ? *m_snapshot : empty, m_view, static_cast<int>(width() * ratio),
                               static_cast<int>(height() * ratio), style);
        m_image = look::toImage(m_frame);
        m_image.setDevicePixelRatio(ratio);
        m_renderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        m_dirty = false;
    }
    QPainter painter(this);
    painter.drawImage(0, 0, m_image);
    painter.setPen(QColor(150, 156, 165));
    QFont font("Roboto");
    font.setPixelSize(11);
    painter.setFont(font);
    painter.drawText(rect().adjusted(10, 0, -10, -8), Qt::AlignRight | Qt::AlignBottom,
                     QString("render %1 ms").arg(m_renderMs, 0, 'f', 0));
    if (m_stale) {
        painter.setPen(QColor(232, 176, 75));
        font.setPixelSize(13);
        painter.setFont(font);
        painter.drawText(rect().adjusted(12, 10, -12, -10), Qt::AlignLeft | Qt::AlignTop,
                         "Showing the last good build. Fix the diagnostics to update the part");
    }
}

void ViewWidget::resizeEvent(QResizeEvent*)
{
    invalidate();
}

void ViewWidget::mousePressEvent(QMouseEvent* event)
{
    m_last = event->position();
    m_dragButton = event->button();
    m_moved = false;
}

void ViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF delta = event->position() - m_last;
    if (m_dragButton != Qt::NoButton && (m_moved || std::hypot(delta.x(), delta.y()) > 3.0)) {
        m_moved = true;
        if (m_dragButton == Qt::LeftButton) {
            m_view.yaw -= delta.x() * 0.01;
            m_view.pitch = std::clamp(m_view.pitch + delta.y() * 0.01, -1.55, 1.55);
        } else {
            const double ratio = devicePixelRatioF();
            m_view.panX += delta.x() * ratio;
            m_view.panY += delta.y() * ratio;
        }
        m_last = event->position();
        invalidate();
        return;
    }
    if (m_dragButton == Qt::NoButton) {
        const int face = faceUnder(event->position());
        if (face != m_hover) {
            m_hover = face;
            invalidate();
            if (hovered) {
                hovered(face);
            }
        }
    }
}

void ViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_moved && m_dragButton == Qt::LeftButton && clicked) {
        clicked(faceUnder(event->position()));
    }
    m_dragButton = Qt::NoButton;
}

void ViewWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int face = faceUnder(event->position());
    if (face >= 0 && activated) {
        activated(face);
    }
}

void ViewWidget::wheelEvent(QWheelEvent* event)
{
    m_view.zoom *= std::pow(1.1, event->angleDelta().y() / 120.0);
    invalidate();
}

void ViewWidget::leaveEvent(QEvent*)
{
    if (m_hover >= 0) {
        m_hover = -1;
        invalidate();
        if (hovered) {
            hovered(-1);
        }
    }
}

void ViewWidget::keyPressEvent(QKeyEvent* event)
{
    const std::pair<int, const char*> views[] = {
        {Qt::Key_1, "iso"}, {Qt::Key_2, "top"}, {Qt::Key_3, "front"}, {Qt::Key_4, "right"}, {Qt::Key_5, "iso-back"}};
    for (const auto& [key, name] : views) {
        if (event->key() == key) {
            const look::View chosen = *look::namedView(name);
            m_view.yaw = chosen.yaw;
            m_view.pitch = chosen.pitch;
            invalidate();
            return;
        }
    }
    if (event->key() == Qt::Key_F) {
        fit();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace kalve137::studio
