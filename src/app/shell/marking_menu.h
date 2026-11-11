// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QRect>
#include <QWidget>

#include <array>
#include <vector>

class QAction;

namespace kalve137::app {

class MarkingMenu : public QWidget {
    Q_OBJECT

public:
    enum class Slot { North, NorthEast, East, SouthEast, South, SouthWest, West, NorthWest };

    explicit MarkingMenu(QWidget* parent = nullptr);

    void setSlot(Slot slot, QAction* action);
    void addListAction(QAction* action);
    void addListSeparator();
    void popup(QPoint center);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Item {
        QAction* action = nullptr;
        QRect rect;
    };

    void layoutItems();
    int itemAt(QPoint pos) const;
    void setHovered(int index);
    void trigger(int index);

    static constexpr int kSlotCount = 8;
    std::array<QAction*, kSlotCount> m_slots{};
    std::vector<QAction*> m_list;

    std::vector<Item> m_items;
    std::vector<QRect> m_separators;
    QRect m_listRect;
    QPoint m_center;
    int m_hovered = -1;
    bool m_dragged = false;
};

} // namespace kalve137::app
