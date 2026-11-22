// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/expression.h"

#include <QObject>

#include <functional>
#include <vector>

class QAction;
class QLineEdit;

namespace kalve137::app {

std::vector<double> recentValues(core::Quantity quantity);
void rememberValue(core::Quantity quantity, double value);

class RecentValuesMenu : public QObject {
    Q_OBJECT

public:
    RecentValuesMenu(QLineEdit* edit, std::function<core::Quantity()> quantity, std::function<void(double)> choose);

    void reset();
    void commit(double value);
    void open();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();

    QLineEdit* m_edit;
    QAction* m_action;
    std::function<core::Quantity()> m_quantity;
    std::function<void(double)> m_choose;
    bool m_edited = false;
};

} // namespace kalve137::app
