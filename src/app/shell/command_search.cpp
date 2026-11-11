// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/command_search.h"

#include "app/shell/shortcuts_dialog.h"
#include "app/theme/theme.h"

#include <QAction>
#include <QCursor>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QScreen>
#include <QStyle>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

namespace kalve137::app {

namespace {

constexpr int kPopupWidth = theme::kWidthNarrow;
constexpr int kVisibleRows = 10;
constexpr int kRowHeight = theme::kDensity.rowHeight;
constexpr int kMargin = theme::kSpaceS;
constexpr int kCommandRole = Qt::UserRole;

} // namespace

std::optional<int> commandMatchRank(const QString& name, const QString& query)
{
    const QString wanted = query.trimmed();
    if (wanted.isEmpty() || name.startsWith(wanted, Qt::CaseInsensitive)) {
        return 0;
    }
    const QStringList words = name.split(' ', Qt::SkipEmptyParts);
    for (qsizetype i = 1; i < words.size(); ++i) {
        if (words[i].startsWith(wanted, Qt::CaseInsensitive)) {
            return 1;
        }
    }
    if (name.contains(wanted, Qt::CaseInsensitive)) {
        return 2;
    }
    return std::nullopt;
}

CommandSearch::CommandSearch(QWidget* window)
    : QFrame(window, Qt::Popup), m_window(window), m_open(new QAction(tr("Search Commands..."), window)),
      m_query(new QLineEdit(this)), m_list(new QTreeWidget(this))
{
    setObjectName(QStringLiteral("commandSearch"));
    m_open->setShortcut(QKeySequence(Qt::Key_S));
    connect(m_open, &QAction::triggered, this, &CommandSearch::popup);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    layout->setSpacing(kMargin);
    m_query->setPlaceholderText(tr("Search commands"));
    m_query->installEventFilter(this);
    layout->addWidget(m_query);
    m_list->setColumnCount(2);
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->setFixedHeight(kVisibleRows * kRowHeight);
    layout->addWidget(m_list);
    setFixedWidth(kPopupWidth);

    connect(m_query, &QLineEdit::textChanged, this, &CommandSearch::refilter);
    connect(m_list, &QTreeWidget::itemClicked, this, &CommandSearch::runCurrent);
}

void CommandSearch::popup()
{
    collect();
    m_query->clear();
    refilter();
    adjustSize();
    QRect area = m_window->screen()->availableGeometry();
    QRect frame(QCursor::pos() - QPoint(width() / 2, m_query->height() / 2), size());
    frame.moveLeft(std::clamp(frame.left(), area.left(), area.right() - frame.width()));
    frame.moveTop(std::clamp(frame.top(), area.top(), area.bottom() - frame.height()));
    move(frame.topLeft());
    show();
    m_query->setFocus();
}

void CommandSearch::collect()
{
    std::map<QString, QAction*> byName;
    for (QAction* action : m_window->findChildren<QAction*>()) {
        if (action == m_open || action->isSeparator() || action->menu() != nullptr || !action->isEnabled() ||
            !action->isVisible() || action->property(kHiddenFromCommandSearch).toBool()) {
            continue;
        }
        if (const auto* bar = qobject_cast<QToolBar*>(action->parent()); bar && bar->toggleViewAction() == action) {
            continue;
        }
        const QString name = commandName(action->text());
        if (name.isEmpty()) {
            continue;
        }
        auto [it, added] = byName.emplace(name, action);
        if (!added && it->second->shortcut().isEmpty()) {
            it->second = action;
        }
    }
    m_commands.clear();
    for (const auto& [name, action] : byName) {
        m_commands.push_back({action, name});
    }
}

void CommandSearch::refilter()
{
    struct Row {
        int rank;
        bool plain;
        std::size_t command;
    };
    std::vector<Row> rows;
    for (std::size_t i = 0; i < m_commands.size(); ++i) {
        if (const std::optional<int> rank = commandMatchRank(m_commands[i].name, m_query->text())) {
            rows.push_back({*rank, m_commands[i].action->icon().isNull(), i});
        }
    }
    std::ranges::stable_sort(rows, {}, [](const Row& row) { return std::pair(row.rank, row.plain); });

    m_list->clear();
    const int side = m_list->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, m_list);
    QPixmap blank(side, side);
    blank.fill(Qt::transparent);
    for (const Row& row : rows) {
        const Command& command = m_commands[row.command];
        auto* item =
            new QTreeWidgetItem(m_list, {command.name, command.action->shortcut().toString(QKeySequence::NativeText)});
        item->setIcon(0, row.plain ? QIcon(blank) : command.action->icon());
        item->setData(0, kCommandRole, static_cast<qulonglong>(row.command));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    }
    if (m_list->topLevelItemCount() > 0) {
        m_list->setCurrentItem(m_list->topLevelItem(0));
    }
}

void CommandSearch::runCurrent()
{
    const QTreeWidgetItem* item = m_list->currentItem();
    if (item == nullptr) {
        return;
    }
    const QPointer<QAction> action = m_commands[item->data(0, kCommandRole).toULongLong()].action;
    hide();
    if (action != nullptr && action->isEnabled()) {
        action->trigger();
    }
}

bool CommandSearch::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_query || event->type() != QEvent::KeyPress) {
        return QFrame::eventFilter(watched, event);
    }
    const auto* key = static_cast<QKeyEvent*>(event);
    const int count = m_list->topLevelItemCount();
    const int current = m_list->currentIndex().row();
    switch (key->key()) {
    case Qt::Key_Down:
    case Qt::Key_Up:
        if (count > 0) {
            const int step = key->key() == Qt::Key_Down ? 1 : -1;
            m_list->setCurrentItem(m_list->topLevelItem((current + step + count) % count));
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        runCurrent();
        return true;
    case Qt::Key_Escape:
        hide();
        return true;
    default:
        return QFrame::eventFilter(watched, event);
    }
}

} // namespace kalve137::app
