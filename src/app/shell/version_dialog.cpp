// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/version_dialog.h"

#include "app/session/paths.h"
#include "app/theme/theme.h"

#include "core/history.h"
#include "core/serialization.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>
#include <vector>

namespace kalve137::app {

namespace {

constexpr int kDialogWidth = theme::kWidthRegular;
constexpr int kDialogHeight = 360;

QString describe(const core::Version& version)
{
    const QString when = QLocale().toString(QFileInfo(toQString(version.path)).lastModified(), QLocale::ShortFormat);
    QString features;
    try {
        const std::size_t count = core::peekDesign(version.path).features;
        features = count == 1 ? QDialog::tr("1 feature") : QDialog::tr("%1 features").arg(count);
    } catch (const std::exception&) {
        features = QDialog::tr("unreadable");
    }
    return QDialog::tr("Version %1    %2    %3").arg(version.number).arg(when, features);
}

} // namespace

std::optional<std::filesystem::path> chooseVersion(QWidget* parent, const std::filesystem::path& design)
{
    const std::vector<core::Version> versions = core::versions(design);

    QDialog dialog(parent);
    dialog.setWindowTitle(QDialog::tr("Version History"));
    dialog.resize(kDialogWidth, kDialogHeight);
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(&dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* list = new QListWidget(&dialog);
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* restore = buttons->addButton(QDialog::tr("Restore"), QDialogButtonBox::AcceptRole);
    restore->setObjectName("primaryButton");
    layout->addWidget(buttons);

    if (versions.empty()) {
        intro->setText(QDialog::tr("No earlier versions yet. Each save keeps the one before it here."));
        list->hide();
        restore->setEnabled(false);
    } else {
        intro->setText(QDialog::tr("Each save keeps the one before it. Restoring replaces the design with "
                                   "the version and can be undone."));
        for (const core::Version& version : versions) {
            list->addItem(describe(version));
        }
        list->setCurrentRow(0);
    }
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted || list->currentRow() < 0) {
        return std::nullopt;
    }
    return versions[static_cast<std::size_t>(list->currentRow())].path;
}

} // namespace kalve137::app
