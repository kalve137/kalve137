// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/marking_menu.h"

#include "app/theme/theme.h"

#include "viewer/style.h"

#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kalve137::app {

namespace style = viewer::style;

namespace {

constexpr int kRingRadiusX = 120;
constexpr int kRingRadiusY = 80;
constexpr int kDeadZone = 28;
constexpr int kPillHeight = theme::kDensity.controlHeight;
constexpr int kPillPadding = theme::kSpaceL;
constexpr int kPillInset = 14;
constexpr int kIconSize = 16;
constexpr int kIconGap = theme::kSpaceM;
constexpr int kListGap = theme::kSpaceL;
constexpr int kRowHeight = theme::kDensity.rowHeight;
constexpr int kRowPadding = theme::kSpaceL;
constexpr int kSeparatorHeight = 9;
constexpr int kListMinWidth = 190;
constexpr int kShortcutGap = theme::kSpaceXxl;
constexpr int kMargin = theme::kSpaceS;

QString plainText(const QAction* action)
{
    return action->text().remove('&');
}

QString shortcutText(const QAction* action)
{
    return action->shortcut().toString(QKeySequence::NativeText);
}

double slotAngle(int slot)
{
    return slot * std::numbers::pi / 4.0;
}

} // namespace

MarkingMenu::MarkingMenu(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
}

void MarkingMenu::setSlot(Slot slot, QAction* action)
{
    m_slots[static_cast<std::size_t>(slot)] = action;
}

void MarkingMenu::addListAction(QAction* action)
{
    m_list.push_back(action);
}

void MarkingMenu::addListSeparator()
{
    if (!m_list.empty() && m_list.back() != nullptr) {
        m_list.push_back(nullptr);
    }
}

void MarkingMenu::popup(QPoint center)
{
    if (!m_list.empty() && m_list.back() == nullptr) {
        m_list.pop_back();
    }
    layoutItems();
    QRect bounds(QPoint(-kDeadZone, -kDeadZone), QPoint(kDeadZone, kDeadZone));
    for (const Item& item : m_items) {
        bounds |= item.rect;
    }
    bounds |= m_listRect;
    bounds.adjust(-kMargin, -kMargin, kMargin, kMargin);

    const QPoint offset = -bounds.topLeft();
    for (Item& item : m_items) {
        item.rect.translate(offset);
    }
    for (QRect& separator : m_separators) {
        separator.translate(offset);
    }
    m_listRect.translate(offset);
    m_center = offset;
    setGeometry(QRect(center - m_center, bounds.size()));
    show();
}

void MarkingMenu::layoutItems()
{
    const QFontMetrics metrics(font());
    m_items.clear();
    m_separators.clear();

    int ringBottom = kDeadZone;
    for (int slot = 0; slot < kSlotCount; ++slot) {
        QAction* action = m_slots[static_cast<std::size_t>(slot)];
        if (action == nullptr) {
            m_items.push_back({});
            continue;
        }
        const int iconWidth = action->icon().isNull() ? 0 : kIconSize + kIconGap;
        const int width = 2 * kPillPadding + iconWidth + metrics.horizontalAdvance(plainText(action));
        const double angle = slotAngle(slot);
        const QPoint anchor(static_cast<int>(std::lround(kRingRadiusX * std::sin(angle))),
                            static_cast<int>(std::lround(-kRingRadiusY * std::cos(angle))));
        QRect rect(0, 0, width, kPillHeight);
        rect.moveCenter(anchor);
        if (anchor.x() > 0) {
            rect.moveLeft(anchor.x() - kPillInset);
        } else if (anchor.x() < 0) {
            rect.moveRight(anchor.x() + kPillInset);
        }
        ringBottom = std::max(ringBottom, rect.bottom());
        m_items.push_back({action, rect});
    }

    m_listRect = {};
    if (m_list.empty()) {
        return;
    }
    int width = kListMinWidth;
    int height = 0;
    for (const QAction* action : m_list) {
        if (action == nullptr) {
            height += kSeparatorHeight;
            continue;
        }
        const QString shortcut = shortcutText(action);
        width = std::max(width, 2 * kRowPadding + metrics.horizontalAdvance(plainText(action)) +
                                    (shortcut.isEmpty() ? 0 : kShortcutGap + metrics.horizontalAdvance(shortcut)));
        height += kRowHeight;
    }
    m_listRect = QRect(-width / 2, ringBottom + kListGap, width, height + 2);
    int y = m_listRect.top() + 1;
    for (QAction* action : m_list) {
        if (action == nullptr) {
            m_separators.push_back(QRect(m_listRect.left() + 1, y, width - 2, kSeparatorHeight));
            y += kSeparatorHeight;
            continue;
        }
        m_items.push_back({action, QRect(m_listRect.left() + 1, y, width - 2, kRowHeight)});
        y += kRowHeight;
    }
}

int MarkingMenu::itemAt(QPoint pos) const
{
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].action != nullptr && m_items[i].rect.contains(pos)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MarkingMenu::setHovered(int index)
{
    if (m_hovered != index) {
        m_hovered = index;
        update();
    }
}

void MarkingMenu::trigger(int index)
{
    QAction* action = m_items[static_cast<std::size_t>(index)].action;
    if (!action->isEnabled()) {
        return;
    }
    close();
    action->trigger();
}

void MarkingMenu::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    const QPoint fromCenter = pos - m_center;
    const double distance = std::hypot(fromCenter.x(), fromCenter.y());
    if (distance > kDeadZone) {
        m_dragged = true;
    }
    int index = itemAt(pos);
    if (index < 0 && event->buttons() != Qt::NoButton && distance > kDeadZone && !m_listRect.contains(pos)) {
        double angle = std::atan2(static_cast<double>(fromCenter.x()), static_cast<double>(-fromCenter.y()));
        if (angle < 0.0) {
            angle += 2.0 * std::numbers::pi;
        }
        const int slot = static_cast<int>(std::lround(angle / (std::numbers::pi / 4.0))) % kSlotCount;
        if (m_items[static_cast<std::size_t>(slot)].action != nullptr) {
            index = slot;
        }
    }
    setHovered(index);
}

void MarkingMenu::mousePressEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    const QPoint fromCenter = pos - m_center;
    if (!rect().contains(pos) || (itemAt(pos) < 0 && std::hypot(fromCenter.x(), fromCenter.y()) > kDeadZone)) {
        close();
        return;
    }
    setHovered(itemAt(pos));
}

void MarkingMenu::mouseReleaseEvent(QMouseEvent*)
{
    if (m_hovered >= 0) {
        trigger(m_hovered);
        return;
    }
    if (m_dragged) {
        close();
    }
}

void MarkingMenu::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void MarkingMenu::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(0, 0, 0, 1));

    painter.setPen(QPen(theme::kTitaniumDim, style::kStrokeThin));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(m_center), kDeadZone * 0.5, kDeadZone * 0.5);

    if (m_hovered >= 0 && m_hovered < kSlotCount) {
        const QPointF target = m_items[static_cast<std::size_t>(m_hovered)].rect.center();
        const QPointF direction = target - QPointF(m_center);
        const double length = std::hypot(direction.x(), direction.y());
        if (length > 0.0) {
            painter.setPen(QPen(theme::kGold, style::kStrokeRegular, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(m_center) + direction * (kDeadZone * 0.5 / length),
                             QPointF(m_center) + direction * (kDeadZone / length));
        }
    }

    const QFontMetrics metrics(font());
    const qreal ratio = devicePixelRatioF();
    for (int i = 0; i < kSlotCount; ++i) {
        const Item& item = m_items[static_cast<std::size_t>(i)];
        if (item.action == nullptr) {
            continue;
        }
        const bool hovered = i == m_hovered;
        const bool enabled = item.action->isEnabled();
        painter.setPen(QPen(hovered && enabled ? theme::kGold : theme::kSlateEdge, style::kStrokeHairline));
        painter.setBrush(hovered && enabled ? theme::kSlateLight : theme::kSlate);
        const QRectF pill = QRectF(item.rect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);

        const bool iconRight = item.rect.center().x() > m_center.x() + kPillInset;
        QRect icon(0, 0, kIconSize, kIconSize);
        icon.moveCenter(item.rect.center());
        QRect text = item.rect.adjusted(kPillPadding, 0, -kPillPadding, 0);
        if (!item.action->icon().isNull()) {
            if (iconRight) {
                icon.moveRight(text.right());
                text.setRight(icon.left() - kIconGap);
            } else {
                icon.moveLeft(text.left());
                text.setLeft(icon.right() + 1 + kIconGap);
            }
            const QIcon::Mode mode = !enabled ? QIcon::Disabled : (hovered ? QIcon::Active : QIcon::Normal);
            painter.drawPixmap(icon, item.action->icon().pixmap(QSize(kIconSize, kIconSize), ratio, mode));
        }
        painter.setPen(!enabled ? theme::kTitaniumDim : (hovered ? theme::kGold : theme::kTitaniumLight));
        painter.drawText(text, Qt::AlignVCenter | (iconRight ? Qt::AlignRight : Qt::AlignLeft), plainText(item.action));
    }

    if (m_listRect.isNull()) {
        return;
    }
    painter.setPen(QPen(theme::kSlateEdge, style::kStrokeHairline));
    painter.setBrush(theme::kSlate);
    painter.drawRoundedRect(QRectF(m_listRect).adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
    for (const QRect& separator : m_separators) {
        const int y = separator.center().y();
        painter.drawLine(separator.left() + kRowPadding, y, separator.right() - kRowPadding, y);
    }
    for (std::size_t i = kSlotCount; i < m_items.size(); ++i) {
        const Item& item = m_items[i];
        const bool hovered = static_cast<int>(i) == m_hovered;
        const bool enabled = item.action->isEnabled();
        if (hovered && enabled) {
            painter.fillRect(item.rect, theme::kSlateLight);
        }
        const QRect text = item.rect.adjusted(kRowPadding, 0, -kRowPadding, 0);
        painter.setPen(!enabled ? theme::kTitaniumDim : (hovered ? theme::kGold : theme::kTitaniumLight));
        painter.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, plainText(item.action));
        painter.setPen(hovered && enabled ? theme::kTitaniumLight : theme::kTitanium);
        painter.drawText(text, Qt::AlignVCenter | Qt::AlignRight, shortcutText(item.action));
    }
}

} // namespace kalve137::app
