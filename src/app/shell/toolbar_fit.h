// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointer>
#include <QSize>

#include <vector>

class QToolBar;
class QToolButton;

namespace kalve137::app {

class ToolBarFit : public QObject {
public:
    ToolBarFit(QToolBar* bar, std::vector<QToolButton*> labeled);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void fit();
    void setCompact(bool compact);

    QToolBar* m_bar;
    QSize m_iconSize;
    std::vector<QPointer<QToolButton>> m_labeled;
    int m_fullWidth = 0;
    bool m_compact = false;
};

} // namespace kalve137::app
