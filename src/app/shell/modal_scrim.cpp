// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/modal_scrim.h"

#include "app/theme/theme.h"

#include <QApplication>
#include <QEvent>
#include <QPainter>

#include <algorithm>

namespace kalve137::app {

ModalScrim::ModalScrim(QWidget* window) : QWidget(window)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    qApp->installEventFilter(this);
}

void ModalScrim::hold(bool held)
{
    m_holds = std::max(0, m_holds + (held ? 1 : -1));
    refresh();
    if (held && isVisible()) {
        repaint();
    }
}

bool ModalScrim::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
        return false;
    }
    if (event->type() != QEvent::Show && event->type() != QEvent::Hide) {
        return false;
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget != nullptr && widget != window() && widget->isWindow() && widget->isModal()) {
        track(widget, event->type() == QEvent::Show);
    }
    return false;
}

void ModalScrim::track(QWidget* dialog, bool shown)
{
    std::erase_if(m_dialogs, [dialog](const QPointer<QWidget>& open) { return open.isNull() || open == dialog; });
    if (shown) {
        m_dialogs.emplace_back(dialog);
    }
    refresh();
}

void ModalScrim::refresh()
{
    if (m_dialogs.empty() && m_holds == 0) {
        hide();
        return;
    }
    if (!isVisible() && parentWidget()->isVisible()) {
        setGeometry(parentWidget()->rect());
        raise();
        show();
    }
}

void ModalScrim::paintEvent(QPaintEvent*)
{
    QPainter(this).fillRect(rect(), theme::kScrim);
}

ScopedDim::ScopedDim(QWidget* parent)
    : m_scrim(parent != nullptr ? parent->window()->findChild<ModalScrim*>(Qt::FindDirectChildrenOnly) : nullptr)
{
    if (m_scrim != nullptr) {
        m_scrim->hold(true);
    }
}

ScopedDim::~ScopedDim()
{
    if (m_scrim != nullptr) {
        m_scrim->hold(false);
    }
}

} // namespace kalve137::app
