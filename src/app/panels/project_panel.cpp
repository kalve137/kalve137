// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/panels/project_panel.h"

#include "app/session/paths.h"
#include "app/theme/theme.h"

#include "log/log.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace kalve137::app {

namespace {

constexpr int kPathRole = Qt::UserRole;

}

ProjectPanel::ProjectPanel(QWidget* parent) : QWidget(parent)
{
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &ProjectPanel::refresh);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(theme::kPanelMargins);
    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    auto* empty = new QWidget(m_stack);
    auto* emptyLayout = new QVBoxLayout(empty);
    auto* emptyText =
        new QLabel(tr("No project open. A project is a folder on this computer that holds your designs."), empty);
    emptyText->setWordWrap(true);
    auto* create = new QPushButton(tr("New Project..."), empty);
    auto* open = new QPushButton(tr("Open Project..."), empty);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addWidget(create);
    emptyLayout->addWidget(open);
    emptyLayout->addStretch();
    m_stack->addWidget(empty);
    connect(create, &QPushButton::clicked, this, &ProjectPanel::newProjectRequested);
    connect(open, &QPushButton::clicked, this, &ProjectPanel::openProjectRequested);

    auto* page = new QWidget(m_stack);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    m_name = new QLabel(page);
    m_name->setObjectName("panelTitle");
    m_folder = new QLabel(page);
    m_folder->setObjectName("panelHint");
    m_folder->setWordWrap(true);
    m_folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_designs = new QListWidget(page);
    auto* buttons = new QHBoxLayout();
    auto* newDesign = new QPushButton(tr("New Design"), page);
    auto* showFolder = new QPushButton(tr("Show Folder"), page);
    buttons->addWidget(newDesign);
    buttons->addWidget(showFolder);
    pageLayout->addWidget(m_name);
    pageLayout->addWidget(m_folder);
    pageLayout->addWidget(m_designs, 1);
    pageLayout->addLayout(buttons);
    m_stack->addWidget(page);

    connect(newDesign, &QPushButton::clicked, this, &ProjectPanel::newDesignRequested);
    connect(showFolder, &QPushButton::clicked, this, [this] {
        if (m_project) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(m_project->root())));
        }
    });
    connect(m_designs, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const std::filesystem::path path = toPath(item->data(kPathRole).toString());
        QTimer::singleShot(0, this, [this, path] { emit openDesignRequested(path); });
    });

    refresh();
}

void ProjectPanel::setProject(std::optional<core::Project> project)
{
    if (!m_watcher->directories().isEmpty()) {
        m_watcher->removePaths(m_watcher->directories());
    }
    m_project = std::move(project);
    if (m_project) {
        m_watcher->addPath(toQString(m_project->root()));
    }
    refresh();
}

void ProjectPanel::setCurrentDesign(const std::filesystem::path& path)
{
    m_current = path;
    refresh();
}

void ProjectPanel::refresh()
{
    if (!m_project) {
        m_stack->setCurrentIndex(0);
        return;
    }
    m_stack->setCurrentIndex(1);
    m_name->setText(QString::fromStdString(m_project->name()));
    const QString folder = QDir::toNativeSeparators(toQString(m_project->root()));
    m_folder->setText(QString(folder).replace(QDir::separator(), QString(QDir::separator()) + QChar(0x200B)));
    m_folder->setToolTip(folder);
    m_designs->clear();
    std::vector<std::filesystem::path> designs;
    try {
        designs = m_project->designs();
    } catch (const std::exception& failure) {
        K137_WARN(Io, "Listing the project's designs failed: {}", failure.what());
    }
    std::error_code error;
    for (const std::filesystem::path& design : designs) {
        auto* item = new QListWidgetItem(toQString(design.stem()), m_designs);
        item->setData(kPathRole, toQString(design));
        if (!m_current.empty() && std::filesystem::equivalent(design, m_current, error)) {
            QFont font = item->font();
            font.setWeight(QFont::Medium);
            item->setFont(font);
        }
    }
}

} // namespace kalve137::app
