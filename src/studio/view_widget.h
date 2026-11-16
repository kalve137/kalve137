// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "look/raster.h"
#include "model/snapshot.h"

#include <QImage>
#include <QWidget>

#include <functional>
#include <memory>
#include <set>

namespace kalve137::studio {

class ViewWidget : public QWidget {
public:
    explicit ViewWidget(QWidget* parent = nullptr);

    std::function<void(int face)> hovered;
    std::function<void(int face)> clicked;
    std::function<void(int face)> activated;

    void setSnapshot(std::shared_ptr<const model::Snapshot> snapshot, bool stale);
    void setSelected(std::set<int> faces);
    void fit();
    const model::Snapshot* snapshot() const { return m_snapshot.get(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    int faceUnder(QPointF position) const;
    void invalidate();

    std::shared_ptr<const model::Snapshot> m_snapshot;
    bool m_stale = false;
    bool m_fitted = false;
    look::View m_view;
    look::Frame m_frame;
    QImage m_image;
    bool m_dirty = true;
    int m_hover = -1;
    std::set<int> m_selected;
    QPointF m_last;
    Qt::MouseButton m_dragButton = Qt::NoButton;
    bool m_moved = false;
    double m_renderMs = 0.0;
};

} // namespace kalve137::studio
