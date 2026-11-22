// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/toolbar_fit.h"

#include <QEvent>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>

namespace kalve137::app {

namespace {

constexpr int kCompactIconSize = 24;

} // namespace

ToolBarFit::ToolBarFit(QToolBar* bar, std::vector<QToolButton*> labeled)
    : QObject(bar), m_bar(bar), m_iconSize(bar->iconSize())
{
    m_labeled.assign(labeled.begin(), labeled.end());
    bar->installEventFilter(this);
}

bool ToolBarFit::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_bar && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        fit();
    }
    return false;
}

void ToolBarFit::fit()
{
    if (!m_compact) {
        m_fullWidth = m_bar->sizeHint().width();
    }
    setCompact(m_fullWidth > m_bar->width());
}

void ToolBarFit::setCompact(bool compact)
{
    if (compact == m_compact) {
        return;
    }
    m_compact = compact;
    const QSize iconSize = compact ? QSize(kCompactIconSize, kCompactIconSize) : m_iconSize;
    m_bar->setIconSize(iconSize);
    for (const QPointer<QToolButton>& button : m_labeled) {
        if (button) {
            button->setIconSize(iconSize);
            button->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon);
        }
    }
    m_bar->setProperty("compact", compact);
    for (QToolButton* button : m_bar->findChildren<QToolButton*>()) {
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
}

} // namespace kalve137::app
