// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/recent_values.h"

#include "app/feature/value_edit.h"
#include "app/theme/icons.h"

#include <QAction>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <utility>

namespace kalve137::app {

namespace {

constexpr int kMaxRecentValues = 5;
constexpr int kMenuDecimals = 4;
constexpr double kSameValue = 1e-9;

QString keyOf(core::Quantity quantity)
{
    switch (quantity) {
    case core::Quantity::Length:
        return QStringLiteral("recentValues/length");
    case core::Quantity::Angle:
        return QStringLiteral("recentValues/angle");
    case core::Quantity::Count:
        return QStringLiteral("recentValues/count");
    }
    return QStringLiteral("recentValues/length");
}

} // namespace

std::vector<double> recentValues(core::Quantity quantity)
{
    std::vector<double> values;
    for (const QVariant& entry : QSettings().value(keyOf(quantity)).toList()) {
        bool ok = false;
        const double value = entry.toDouble(&ok);
        if (ok && std::isfinite(value) && std::ssize(values) < kMaxRecentValues) {
            values.push_back(value);
        }
    }
    return values;
}

void rememberValue(core::Quantity quantity, double value)
{
    if (!std::isfinite(value)) {
        return;
    }
    std::vector<double> values = recentValues(quantity);
    std::erase_if(values, [value](double v) { return std::abs(v - value) <= kSameValue * std::max(1.0, std::abs(v)); });
    values.insert(values.begin(), value);
    if (std::ssize(values) > kMaxRecentValues) {
        values.resize(kMaxRecentValues);
    }
    QVariantList list;
    for (const double v : values) {
        list.push_back(v);
    }
    QSettings().setValue(keyOf(quantity), list);
}

RecentValuesMenu::RecentValuesMenu(QLineEdit* edit, std::function<core::Quantity()> quantity,
                                   std::function<void(double)> choose)
    : QObject(edit), m_edit(edit), m_quantity(std::move(quantity)), m_choose(std::move(choose))
{
    m_action = edit->addAction(icons::fixed(QStringLiteral("qss/dark/chevron_down")), QLineEdit::TrailingPosition);
    m_action->setObjectName("recentValues");
    m_action->setToolTip(tr("Recent values (Alt+Down)"));
    m_action->setVisible(false);
    connect(m_action, &QAction::triggered, this, &RecentValuesMenu::open);
    connect(edit, &QLineEdit::textEdited, this, [this] { m_edited = true; });
    edit->installEventFilter(this);
}

void RecentValuesMenu::reset()
{
    m_edited = false;
    refresh();
}

void RecentValuesMenu::commit(double value)
{
    if (std::exchange(m_edited, false)) {
        rememberValue(m_quantity(), value);
    }
    refresh();
}

void RecentValuesMenu::open()
{
    const core::Quantity quantity = m_quantity();
    const std::vector<double> values = recentValues(quantity);
    if (values.empty()) {
        return;
    }
    QMenu menu(m_edit);
    menu.setObjectName("recentValuesMenu");
    for (const double value : values) {
        QAction* item = menu.addAction(formatValue(value, quantity, kMenuDecimals));
        connect(item, &QAction::triggered, this, [this, value] {
            m_edited = true;
            m_choose(value);
        });
    }
    menu.exec(m_edit->mapToGlobal(m_edit->rect().bottomLeft()));
    m_edit->setFocus();
}

bool RecentValuesMenu::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_edit) {
        if (event->type() == QEvent::Show) {
            refresh();
        } else if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Down && key->modifiers() == Qt::AltModifier) {
                open();
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void RecentValuesMenu::refresh()
{
    m_action->setVisible(!recentValues(m_quantity()).empty());
}

} // namespace kalve137::app
