// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/band_overlay.h"

#include "render/box_select.h"
#include "viewer/style.h"

#include <QEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>

namespace kalve137::app {

BandOverlay::BandOverlay(QWidget* view) : QWidget(view)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setGeometry(view->rect());
    view->installEventFilter(this);
    hide();
}

void BandOverlay::showBand(QPointF from, QPointF to)
{
    m_from = from;
    m_to = to;
    setGeometry(parentWidget()->rect());
    raise();
    show();
    update();
}

void BandOverlay::hideBand()
{
    hide();
}

bool BandOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(watched, event);
}

void BandOverlay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    QColor fill = m_color;
    fill.setAlphaF(static_cast<float>(1.0 - viewer::style::kTintFaint));
    QPen pen(m_color, viewer::style::kStrokeHairline);
    pen.setCosmetic(true);
    pen.setStyle(render::isCrossingBox({m_from.x(), m_from.y()}, {m_to.x(), m_to.y()}) ? Qt::DashLine : Qt::SolidLine);
    painter.setPen(pen);
    painter.setBrush(fill);
    painter.drawRect(QRectF(m_from, m_to).normalized());
}

} // namespace kalve137::app
