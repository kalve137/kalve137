// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QPointF>
#include <QWidget>

namespace kalve137::app {

class BandOverlay : public QWidget {
    Q_OBJECT

public:
    explicit BandOverlay(QWidget* view);

    void setColor(const QColor& color) { m_color = color; }
    void showBand(QPointF from, QPointF to);
    void hideBand();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QColor m_color;
    QPointF m_from;
    QPointF m_to;
};

} // namespace kalve137::app
