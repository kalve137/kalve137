// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/main_window.h"

#include "app/feature/feature_panel.h"
#include "app/panels/project_panel.h"
#include "app/session/autosave.h"
#include "app/session/design_session.h"
#include "app/session/paths.h"
#include "app/shell/command_search.h"
#include "app/shell/modal_scrim.h"
#include "app/shell/version_dialog.h"
#include "app/theme/theme.h"

#include "core/history.h"
#include "core/serialization.h"
#include "geometry/exchange.h"
#include "log/log.h"
#include "viewer/occt_view_widget.h"

#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <optional>
#include <stdexcept>

namespace kalve137::app {

namespace {

constexpr int kMaxRecentProjects = 8;
const char* const kRecentProjectsKey = "recentProjects";

QString defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QStringList recentProjects()
{
    return QSettings().value(kRecentProjectsKey).toStringList();
}

class BusyCursor {
public:
    BusyCursor() { QGuiApplication::setOverrideCursor(Qt::WaitCursor); }
    ~BusyCursor() { QGuiApplication::restoreOverrideCursor(); }
    BusyCursor(const BusyCursor&) = delete;
    BusyCursor& operator=(const BusyCursor&) = delete;
};

constexpr int kImportProgressSteps = 1000;
constexpr int kImportDialogDelayMs = 300;
constexpr int kImportPollMs = 50;
constexpr int kImportDialogWidth = theme::kWidthNarrow;

class ImportDialog : public QDialog {
public:
    ImportDialog(QWidget* parent, const QString& fileName, std::function<void()> onCancel)
        : QDialog(parent, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
          m_label(new QLabel(MainWindow::tr("Importing %1...").arg(fileName), this)), m_bar(new QProgressBar(this)),
          m_cancel(new QPushButton(MainWindow::tr("Cancel"), this)), m_onCancel(std::move(onCancel))
    {
        setWindowTitle(MainWindow::tr("Import STEP"));
        setWindowModality(Qt::WindowModal);
        m_bar->setRange(0, 0);
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(m_label);
        layout->addWidget(m_bar);
        auto* buttons = new QHBoxLayout;
        buttons->addStretch();
        buttons->addWidget(m_cancel);
        layout->addLayout(buttons);
        setFixedWidth(std::max(sizeHint().width(), kImportDialogWidth));
        connect(m_cancel, &QPushButton::clicked, this, &ImportDialog::cancel);
    }

    void setFraction(double done)
    {
        if (m_cancel->isEnabled()) {
            m_bar->setRange(0, kImportProgressSteps);
            m_bar->setValue(static_cast<int>(done * kImportProgressSteps));
        }
    }

    void reject() override { cancel(); }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        event->ignore();
        cancel();
    }

private:
    void cancel()
    {
        if (!m_cancel->isEnabled()) {
            return;
        }
        m_cancel->setEnabled(false);
        m_label->setText(MainWindow::tr("Stopping the import..."));
        m_bar->setRange(0, 0);
        m_onCancel();
    }

    QLabel* m_label;
    QProgressBar* m_bar;
    QPushButton* m_cancel;
    std::function<void()> m_onCancel;
};

std::vector<geometry::StepPart> readStepInBackground(QWidget* parent, const std::filesystem::path& path)
{
    std::atomic<double> fraction = 0.0;
    std::atomic<bool> stop = false;
    auto task = std::async(std::launch::async, [&] {
        return geometry::readStepParts(path, [&](double done) {
            fraction = done;
            return !stop;
        });
    });

    ImportDialog dialog(parent, toQString(path.filename()), [&] { stop = true; });
    QTimer::singleShot(kImportDialogDelayMs, &dialog, [&] { dialog.show(); });
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            loop.quit();
            return;
        }
        if (const double done = fraction; done > 0.0) {
            dialog.setFraction(done);
        }
    });
    poll.start(kImportPollMs);
    loop.exec();
    return task.get();
}

void reportFailure(QWidget* parent, const QString& title, const std::exception& error)
{
    K137_ERROR(Io, "{}: {}", title.toStdString(), error.what());
    QMessageBox::critical(parent, title, QString::fromUtf8(error.what()));
}

void rememberProject(const std::filesystem::path& root)
{
    QStringList list = recentProjects();
    const QString path = toQString(root);
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecentProjects) {
        list.removeLast();
    }
    QSettings().setValue(kRecentProjectsKey, list);
}

} // namespace

void MainWindow::restoreSession()
{
    const QStringList recent = recentProjects();
    if (m_preferences.reopenLastProject && !recent.isEmpty() &&
        core::Project::isProjectFolder(toPath(recent.front()))) {
        openProjectAt(toPath(recent.front()));
    }
    updateRecentProjects();
    QTimer::singleShot(0, this, [this] {
        offerCrashReport();
        offerRecovery();
    });
}

void MainWindow::offerRecovery()
{
    std::optional<Recovery> recovery = Autosave::findRecovery();
    if (!recovery) {
        return;
    }
    const QString name = recovery->name.isEmpty() ? tr("Untitled") : recovery->name;
    QMessageBox box(this);
    box.setWindowTitle(tr("Recover Design"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("Kalve 137 closed before %1 was saved.").arg(name));
    box.setInformativeText(tr("Changes up to %1 were kept. Recover them?")
                               .arg(QLocale().toString(recovery->savedAt, QLocale::ShortFormat)));
    QPushButton* recover = box.addButton(tr("Recover"), QMessageBox::AcceptRole);
    box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.setDefaultButton(recover);
    box.exec();
    K137_INFO(App, "Recovery offered for {}, {}", name.toStdString(),
              box.clickedButton() == recover ? "recovered" : "discarded");
    if (box.clickedButton() == recover) {
        leaveModes();
        m_session->recover(std::move(recovery->document), recovery->source);
        m_view->homeView();
        fitView();
        statusBar()->showMessage(tr("Recovered %1, save to keep it").arg(name), kMessageTimeoutMs);
    }
    Autosave::remove(*recovery);
}

void MainWindow::showVersionHistory()
{
    if (m_session->path().empty()) {
        QMessageBox::information(this, tr("Version History"),
                                 tr("Save the design first. Every later save keeps the one before it."));
        return;
    }
    const std::optional<std::filesystem::path> chosen = chooseVersion(this, m_session->path());
    if (!chosen) {
        return;
    }
    core::Document document;
    try {
        document = core::loadDesign(*chosen, shapesOf(m_session->path()));
    } catch (const std::exception& error) {
        reportFailure(this, tr("Restore Failed"), error);
        return;
    }
    K137_INFO(App, "Restoring version {}", log::utf8(*chosen));
    leaveModes();
    m_session->edit(tr("Restore %1").arg(toQString(chosen->stem())),
                    [&](core::Document& current) { current = std::move(document); });
    statusBar()->showMessage(tr("Restored %1, undo to go back").arg(toQString(chosen->stem())), kMessageTimeoutMs);
}

bool MainWindow::maybeSave()
{
    if (!m_session->isDirty()) {
        return true;
    }
    const auto answer =
        QMessageBox::question(this, tr("Kalve 137"), tr("Save changes to %1?").arg(m_session->displayName()),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) {
        return save();
    }
    return answer == QMessageBox::Discard;
}

bool MainWindow::save()
{
    if (m_session->path().empty()) {
        return saveAs();
    }
    return writeDesign(m_session->path());
}

core::ShapeStore& MainWindow::shapesOf(const std::filesystem::path& design)
{
    m_shapes.setFolder(core::shapeFolder(design));
    return m_shapes;
}

bool MainWindow::saveAs()
{
    QString folder = m_project ? toQString(m_project->root()) : defaultFolder();
    if (!m_session->path().empty()) {
        folder = toQString(m_session->path().parent_path());
    }
    const QString suggested = folder + "/" + m_session->displayName() + core::kDesignExtension;
    QString file = dimmed(this, [&] {
        return QFileDialog::getSaveFileName(this, tr("Save Design"), suggested, tr("Kalve 137 design (*.k137)"));
    });
    if (file.isEmpty()) {
        return false;
    }
    std::filesystem::path path = toPath(file);
    if (path.extension() != core::kDesignExtension) {
        path += core::kDesignExtension;
    }
    return writeDesign(path);
}

bool MainWindow::writeDesign(const std::filesystem::path& path)
{
    if (m_session->isDirty() || path != m_session->path()) {
        try {
            core::archiveVersion(path);
        } catch (const std::exception& error) {
            K137_WARN(Io, "Keeping the previous version failed: {}", error.what());
        }
    }
    try {
        core::saveDesign(m_session->document(), path, shapesOf(path));
    } catch (const std::exception& error) {
        reportFailure(this, tr("Save Failed"), error);
        return false;
    }
    try {
        core::pruneShapes(path);
    } catch (const std::exception& error) {
        K137_WARN(Io, "Removing unused shapes failed: {}", error.what());
    }
    m_session->markSaved(path);
    statusBar()->showMessage(tr("Saved %1").arg(toQString(path.filename())), kMessageTimeoutMs);
    return true;
}

void MainWindow::newDesign()
{
    if (!maybeSave()) {
        return;
    }
    leaveModes();
    m_session->reset(core::Document{}, {});
    m_view->homeView();
    fitView();
}

void MainWindow::openDesign()
{
    if (!maybeSave()) {
        return;
    }
    const QString folder = m_project ? toQString(m_project->root()) : defaultFolder();
    const QString file = dimmed(this, [&] {
        return QFileDialog::getOpenFileName(this, tr("Open Design"), folder, tr("Kalve 137 design (*.k137)"));
    });
    if (!file.isEmpty()) {
        openDesignAt(toPath(file));
    }
}

void MainWindow::openDesignAt(const std::filesystem::path& path)
{
    core::Document document;
    try {
        document = core::loadDesign(path, shapesOf(path));
    } catch (const std::exception& error) {
        reportFailure(this, tr("Open Failed"), error);
        return;
    }
    leaveModes();
    const std::filesystem::path folder = path.parent_path();
    std::error_code ec;
    if (core::Project::isProjectFolder(folder) &&
        (!m_project || !std::filesystem::equivalent(m_project->root(), folder, ec))) {
        try {
            setProject(core::Project::open(folder));
        } catch (const std::exception& error) {
            K137_WARN(Io, "Opening the design's project failed: {}", error.what());
        }
    }
    m_session->reset(std::move(document), path);
    m_view->homeView();
    fitView();
}

void MainWindow::newProject()
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("New Project"), tr("Project name"), QLineEdit::Normal, tr("My Project"), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    const QString parent = dimmed(this, [&] {
        return QFileDialog::getExistingDirectory(this, tr("Where should the project folder go?"), defaultFolder());
    });
    if (parent.isEmpty()) {
        return;
    }
    const std::filesystem::path folder = toPath(parent) / toPath(name);
    try {
        setProject(core::Project::create(folder, name.toStdString()));
    } catch (const std::exception& error) {
        reportFailure(this, tr("New Project Failed"), error);
    }
}

void MainWindow::openProject()
{
    const QString folder = dimmed(this, [&] {
        return QFileDialog::getExistingDirectory(this, tr("Open Project Folder"),
                                                 m_project ? toQString(m_project->root()) : defaultFolder());
    });
    if (folder.isEmpty()) {
        return;
    }
    const std::filesystem::path path = toPath(folder);
    if (!core::Project::isProjectFolder(path)) {
        const auto answer = QMessageBox::question(
            this, tr("Open Project"),
            tr("This folder is not a Kalve 137 project yet. Make it one? Existing files stay as they are."));
        if (answer != QMessageBox::Yes) {
            return;
        }
        try {
            setProject(core::Project::create(path, toQString(path.filename()).toStdString()));
        } catch (const std::exception& error) {
            reportFailure(this, tr("Open Project Failed"), error);
        }
        return;
    }
    openProjectAt(path);
}

void MainWindow::openProjectAt(const std::filesystem::path& folder)
{
    try {
        setProject(core::Project::open(folder));
    } catch (const std::exception& error) {
        reportFailure(this, tr("Open Project Failed"), error);
    }
}

void MainWindow::setProject(std::optional<core::Project> project)
{
    m_project = std::move(project);
    if (m_project) {
        rememberProject(m_project->root());
        updateRecentProjects();
    }
    m_projectPanel->setProject(m_project);
    updateTitle();
}

void MainWindow::updateRecentProjects()
{
    m_recentMenu->clear();
    for (const QString& path : recentProjects()) {
        if (!core::Project::isProjectFolder(toPath(path))) {
            continue;
        }
        QAction* recent = m_recentMenu->addAction(QDir::toNativeSeparators(path), this,
                                                  [this, path] { openProjectAt(toPath(path)); });
        recent->setProperty(kHiddenFromCommandSearch, true);
    }
    m_recentMenu->setEnabled(!m_recentMenu->isEmpty());
}

std::filesystem::path MainWindow::exportPath(const QString& extension, const QString& filter)
{
    if (m_session->model().bodies.empty()) {
        QMessageBox::information(this, tr("Export"), tr("There are no bodies to export yet."));
        return {};
    }
    QString folder = defaultFolder();
    if (m_project) {
        try {
            folder = toQString(m_project->exportsFolder());
        } catch (const std::exception&) {
            folder = toQString(m_project->root());
        }
    } else if (!m_session->path().empty()) {
        folder = toQString(m_session->path().parent_path());
    }
    const QString file = dimmed(this, [&] {
        return QFileDialog::getSaveFileName(this, tr("Export"), folder + "/" + m_session->displayName() + extension,
                                            filter);
    });
    if (file.isEmpty()) {
        return {};
    }
    std::filesystem::path path = toPath(file);
    if (path.extension().empty()) {
        path += extension.toStdString();
    }
    return path;
}

void MainWindow::importStep()
{
    const QString folder = m_project ? toQString(m_project->root()) : defaultFolder();
    const QString file = dimmed(
        this, [&] { return QFileDialog::getOpenFileName(this, tr("Import STEP"), folder, tr("STEP (*.step *.stp)")); });
    if (file.isEmpty()) {
        return;
    }
    const std::filesystem::path path = toPath(file);
    const QString name = toQString(path.stem());
    statusBar()->showMessage(tr("Importing %1...").arg(toQString(path.filename())));
    std::vector<geometry::StepPart> parts;
    try {
        parts = readStepInBackground(this, path);
    } catch (const geometry::ImportCanceled&) {
        statusBar()->showMessage(tr("Import canceled"), kMessageTimeoutMs);
        return;
    } catch (const std::exception& error) {
        statusBar()->clearMessage();
        reportFailure(this, tr("Import Failed"), error);
        return;
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].name.empty()) {
            parts[i].name = (parts.size() == 1 ? name : QStringLiteral("%1 %2").arg(name).arg(i + 1)).toStdString();
        }
    }
    leaveModes();
    {
        const BusyCursor busy;
        m_session->edit(tr("Import %1").arg(name), [&](core::Document& document) {
            document.add(core::ImportFeature{toQString(path.filename()).toStdString(), std::move(parts)});
        });
        fitView();
    }
    statusBar()->showMessage(tr("Imported %1").arg(toQString(path.filename())), kMessageTimeoutMs);
}

void MainWindow::exportStep()
{
    const std::filesystem::path path = exportPath(QStringLiteral(".step"), tr("STEP (*.step *.stp)"));
    if (path.empty()) {
        return;
    }
    try {
        geometry::writeStep(m_session->model().stepParts(), path, std::string("Kalve 137 ") + KALVE137_VERSION);
        statusBar()->showMessage(tr("Exported %1").arg(toQString(path.filename())), kMessageTimeoutMs);
    } catch (const std::exception& error) {
        reportFailure(this, tr("Export Failed"), error);
    }
}

void MainWindow::exportStl()
{
    const std::filesystem::path path = exportPath(QStringLiteral(".stl"), tr("STL (*.stl)"));
    if (path.empty()) {
        return;
    }
    try {
        geometry::writeStl(m_session->model().shapes(), path);
        statusBar()->showMessage(tr("Exported %1").arg(toQString(path.filename())), kMessageTimeoutMs);
    } catch (const std::exception& error) {
        reportFailure(this, tr("Export Failed"), error);
    }
}

} // namespace kalve137::app
