// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFrame>
#include <QPointer>

#include <optional>
#include <vector>

class QAction;
class QLineEdit;
class QTreeWidget;

namespace kalve137::app {

inline constexpr const char* kHiddenFromCommandSearch = "hiddenFromCommandSearch";

std::optional<int> commandMatchRank(const QString& name, const QString& query);

class CommandSearch : public QFrame {
    Q_OBJECT

public:
    explicit CommandSearch(QWidget* window);

    QAction* openAction() const { return m_open; }
    void popup();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Command {
        QPointer<QAction> action;
        QString name;
    };

    void collect();
    void refilter();
    void runCurrent();

    QWidget* m_window;
    QAction* m_open;
    QLineEdit* m_query;
    QTreeWidget* m_list;
    std::vector<Command> m_commands;
};

} // namespace kalve137::app
