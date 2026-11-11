// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/main_window.h"

#include "app/diagnostics/report_dialog.h"
#include "app/session/design_session.h"
#include "app/session/paths.h"
#include "app/shell/shortcuts_dialog.h"

#include "log/log.h"
#include "platform/platform.h"

#include <QAction>
#include <QDesktopServices>
#include <QKeySequence>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>

#include <stdexcept>

namespace kalve137::app {

namespace {

void openFolder(const std::filesystem::path& folder)
{
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(folder)));
}

#ifndef NDEBUG
void addDiagnosticsMenu(QMenu* help, QWidget* parent)
{
    QMenu* menu = help->addMenu(QObject::tr("&Diagnostics"));
    for (log::Channel channel : log::kChannels) {
        const QString name =
            QString::fromUtf8(log::toString(channel).data(), static_cast<qsizetype>(log::toString(channel).size()));
        QAction* action = menu->addAction(QObject::tr("Trace %1").arg(name));
        action->setCheckable(true);
        action->setChecked(log::level(channel) == log::Level::trace);
        QObject::connect(menu, &QMenu::aboutToShow, action,
                         [action, channel] { action->setChecked(log::level(channel) == log::Level::trace); });
        QObject::connect(action, &QAction::toggled, action,
                         [channel](bool on) { log::setLevel(channel, on ? log::Level::trace : log::Level::debug); });
    }
    menu->addSeparator();
    menu->addAction(QObject::tr("Open Live Log"), parent, [] {
        log::flush();
        QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(log::currentFile())));
    });
    menu->addAction(QObject::tr("Throw an Exception"), parent,
                    [] { throw std::runtime_error("Test exception from Help > Diagnostics"); });
    menu->addAction(QObject::tr("Crash Now"), parent, [parent] {
        if (QMessageBox::question(parent, QObject::tr("Crash Now"),
                                  QObject::tr("Kalve 137 will crash and write a dump. Unsaved changes are lost "
                                              "except for autosave. Continue?")) == QMessageBox::Yes) {
            K137_WARN(App, "Crashing on request from Help > Diagnostics");
            platform::crashForTesting();
        }
    });
}
#endif

} // namespace

void MainWindow::createHelpMenu()
{
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&Keyboard Shortcuts"), QKeySequence(Qt::CTRL | Qt::Key_Slash), this,
                    [this] { showShortcuts(this); });
    help->addSeparator();
    help->addAction(tr("&Report a Problem..."), this, [this] { showProblemReport(); });
    help->addAction(tr("Open &Log Folder"), this, [] { openFolder(diagnostics::logFolder()); });
#ifndef NDEBUG
    addDiagnosticsMenu(help, this);
#endif
    help->addSeparator();
    help->addAction(tr("&About Kalve 137"), this, &MainWindow::showAbout);
}

void MainWindow::offerCrashReport()
{
    std::optional<diagnostics::Crash> crash = diagnostics::takePendingCrash();
    if (!crash) {
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(tr("Kalve 137 Crashed"));
    box.setIcon(QMessageBox::Warning);
    box.setText(tr("Kalve 137 closed unexpectedly at %1.").arg(QLocale().toString(crash->time, QLocale::ShortFormat)));
    box.setInformativeText(tr("A crash report was saved. Sending it in an issue helps fix the cause."));
    QPushButton* report = box.addButton(tr("Report..."), QMessageBox::AcceptRole);
    box.addButton(tr("Not Now"), QMessageBox::RejectRole);
    box.setDefaultButton(report);
    box.exec();
    if (box.clickedButton() == report) {
        showProblemReport(std::move(crash));
    }
}

void MainWindow::showProblemReport(std::optional<diagnostics::Crash> crash)
{
    app::showProblemReport(this, std::move(crash), m_session->path());
}

} // namespace kalve137::app
