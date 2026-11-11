// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPoint>

#include <vector>

class QWidget;

namespace kalve137::app {

class ViewOverlay : public QObject {
    Q_OBJECT

public:
    explicit ViewOverlay(QWidget* view);

    void pin(QWidget* widget, Qt::Alignment alignment, QPoint margin);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Item {
        QWidget* widget = nullptr;
        Qt::Alignment alignment;
        QPoint margin;
    };

    void place(const Item& item) const;
    void placeAll() const;

    QWidget* m_view;
    std::vector<Item> m_items;
};

} // namespace kalve137::app
