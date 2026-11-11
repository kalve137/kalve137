// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/diagnostics/report_dialog.h"

#include "app/diagnostics/problem_report.h"
#include "app/session/paths.h"
#include "app/theme/theme.h"

#include "log/log.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <exception>

namespace kalve137::app {

namespace {

constexpr QSize kDialogSize{theme::kWidthRegular, 440};

}

void showProblemReport(QWidget* parent, std::optional<diagnostics::Crash> crash, const std::filesystem::path& design)
{
    if (!crash) {
        crash = diagnostics::latestCrash();
    }
    QDialog dialog(parent);
    dialog.setWindowTitle(QDialog::tr("Report a Problem"));
    auto* layout = new QVBoxLayout(&dialog);

    auto* intro = new QLabel(QDialog::tr("Describe what you did and what went wrong. Save the zip and attach it to "
                                         "an issue, then paste the issue text into its description."),
                             &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* description = new QPlainTextEdit(&dialog);
    description->setPlaceholderText(QDialog::tr("Steps to reproduce, what you expected and what happened"));
    layout->addWidget(description, 1);

    auto* logs = new QCheckBox(QDialog::tr("Include the logs"), &dialog);
    logs->setChecked(true);
    auto* system = new QCheckBox(QDialog::tr("Include system information (OS, GPU, memory, versions)"), &dialog);
    system->setChecked(true);
    auto* crashBox = new QCheckBox(&dialog);
    if (crash) {
        crashBox->setText(
            QDialog::tr("Include the crash report from %1").arg(QLocale().toString(crash->time, QLocale::ShortFormat)));
        crashBox->setChecked(true);
    } else {
        crashBox->setText(QDialog::tr("No crash report found"));
        crashBox->setEnabled(false);
    }
    auto* designBox = new QCheckBox(&dialog);
    if (design.empty()) {
        designBox->setText(QDialog::tr("Save the design to include it"));
        designBox->setEnabled(false);
    } else {
        designBox->setText(
            QDialog::tr("Include the design %1 (its last saved state)").arg(toQString(design.filename())));
    }
    for (QCheckBox* box : {logs, system, crashBox, designBox}) {
        layout->addWidget(box);
    }
    auto* status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QPushButton* saveZip = buttons->addButton(QDialog::tr("Save Zip..."), QDialogButtonBox::ActionRole);
    saveZip->setObjectName("primaryButton");
    QPushButton* copy = buttons->addButton(QDialog::tr("Copy Issue Text"), QDialogButtonBox::ActionRole);
    QPushButton* openFolder = buttons->addButton(QDialog::tr("Open Log Folder"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    std::filesystem::path savedZip;
    const auto current = [&] {
        ProblemReport report;
        report.description = description->toPlainText();
        report.includeLogs = logs->isChecked();
        report.includeSystem = system->isChecked();
        if (crashBox->isChecked()) {
            report.crash = crash;
        }
        if (designBox->isChecked()) {
            report.design = design;
        }
        return report;
    };

    QObject::connect(saveZip, &QPushButton::clicked, &dialog, [&] {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        const QString file =
            QFileDialog::getSaveFileName(&dialog, QDialog::tr("Save Problem Report"),
                                         folder + "/" + defaultReportName(), QDialog::tr("Zip archive (*.zip)"));
        if (file.isEmpty()) {
            return;
        }
        try {
            log::writeZip(toPath(file), reportEntries(current()));
        } catch (const std::exception& error) {
            K137_ERROR(App, "Problem report failed: {}", error.what());
            QMessageBox::critical(&dialog, QDialog::tr("Report a Problem"), QString::fromUtf8(error.what()));
            return;
        }
        savedZip = toPath(file);
        K137_INFO(App, "Problem report saved to {}", file.toStdString());
        status->setText(QDialog::tr("Saved %1").arg(file));
    });
    QObject::connect(copy, &QPushButton::clicked, &dialog, [&] {
        QApplication::clipboard()->setText(issueText(current(), savedZip));
        status->setText(QDialog::tr("Issue text copied to the clipboard"));
    });
    QObject::connect(openFolder, &QPushButton::clicked, &dialog,
                     [] { QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(diagnostics::logFolder()))); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(kDialogSize);
    dialog.exec();
}

} // namespace kalve137::app
