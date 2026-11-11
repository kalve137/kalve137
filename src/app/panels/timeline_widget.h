// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/feature.h"

#include <QRect>
#include <QWidget>

#include <cstddef>
#include <optional>
#include <vector>

namespace kalve137::app {

class DesignSession;

class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(DesignSession& session, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    void setEditing(core::FeatureId id);
    std::optional<QRect> chipRect(core::FeatureId id) const;

signals:
    void editRequested(core::FeatureId id);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct Chip {
        core::FeatureId id = 0;
        QRect rect;
    };

    void relayout();
    std::optional<std::size_t> chipAt(QPoint pos) const;
    std::size_t gapAt(int x) const;
    int markerX(std::size_t marker) const;
    void setMarker(std::size_t marker);

    DesignSession& m_session;
    std::vector<Chip> m_chips;
    int m_contentWidth = 0;
    bool m_dragging = false;
    std::size_t m_dragMarker = 0;
    core::FeatureId m_editing = 0;
};

} // namespace kalve137::app
