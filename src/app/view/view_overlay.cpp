// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/view_overlay.h"

#include <QEvent>
#include <QWidget>

#include <algorithm>

namespace kalve137::app {

ViewOverlay::ViewOverlay(QWidget* view) : QObject(view), m_view(view)
{
    view->installEventFilter(this);
}

void ViewOverlay::pin(QWidget* widget, Qt::Alignment alignment, QPoint margin)
{
    widget->setParent(m_view);
    widget->installEventFilter(this);
    m_items.push_back({widget, alignment, margin});
    place(m_items.back());
}

bool ViewOverlay::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (watched == m_view) {
        if (type == QEvent::Resize || type == QEvent::Show) {
            placeAll();
        }
        return false;
    }
    if (type == QEvent::LayoutRequest || type == QEvent::Show) {
        QMetaObject::invokeMethod(this, &ViewOverlay::placeAll, Qt::QueuedConnection);
    }
    return false;
}

void ViewOverlay::place(const Item& item) const
{
    QWidget* widget = item.widget;
    const QSize available = m_view->size() - QSize(2 * item.margin.x(), 2 * item.margin.y());
    const QSize hint = widget->sizeHint()
                           .expandedTo(widget->minimumSizeHint())
                           .expandedTo(widget->minimumSize())
                           .boundedTo(widget->maximumSize());
    const QSize size(std::min(hint.width(), std::max(available.width(), 0)),
                     std::min(hint.height(), std::max(available.height(), 0)));

    int x = (m_view->width() - size.width()) / 2;
    if (item.alignment & Qt::AlignLeft) {
        x = item.margin.x();
    } else if (item.alignment & Qt::AlignRight) {
        x = m_view->width() - size.width() - item.margin.x();
    }
    int y = (m_view->height() - size.height()) / 2;
    if (item.alignment & Qt::AlignTop) {
        y = item.margin.y();
    } else if (item.alignment & Qt::AlignBottom) {
        y = m_view->height() - size.height() - item.margin.y();
    }
    widget->setGeometry(x, y, size.width(), size.height());
}

void ViewOverlay::placeAll() const
{
    for (const Item& item : m_items) {
        place(item);
    }
}

} // namespace kalve137::app
