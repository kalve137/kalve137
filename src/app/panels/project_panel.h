// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/project.h"

#include <QWidget>

#include <filesystem>
#include <optional>

class QFileSystemWatcher;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace kalve137::app {

class ProjectPanel : public QWidget {
    Q_OBJECT

public:
    explicit ProjectPanel(QWidget* parent = nullptr);

    void setProject(std::optional<core::Project> project);
    void setCurrentDesign(const std::filesystem::path& path);

signals:
    void openDesignRequested(const std::filesystem::path& path);
    void newDesignRequested();
    void newProjectRequested();
    void openProjectRequested();

private:
    void refresh();

    std::optional<core::Project> m_project;
    std::filesystem::path m_current;
    QFileSystemWatcher* m_watcher = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_folder = nullptr;
    QListWidget* m_designs = nullptr;
};

} // namespace kalve137::app
