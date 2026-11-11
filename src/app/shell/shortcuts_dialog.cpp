// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/shortcuts_dialog.h"

#include "app/theme/theme.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

namespace kalve137::app {

namespace {

constexpr int kDialogWidth = theme::kWidthRegular;
constexpr int kDialogHeight = 520;

} // namespace

QString commandName(QString text)
{
    text.remove(QStringLiteral("..."));
    for (qsizetype i = text.indexOf('&'); i >= 0; i = text.indexOf('&', i + 1)) {
        text.remove(i, 1);
    }
    return text.trimmed();
}

void showShortcuts(QWidget* window)
{
    std::map<QString, QString> rows;
    for (const QAction* action : window->findChildren<QAction*>()) {
        const QString name = commandName(action->text());
        if (name.isEmpty()) {
            continue;
        }
        QStringList keys;
        for (const QKeySequence& key : action->shortcuts()) {
            keys << key.toString(QKeySequence::NativeText);
        }
        if (!keys.isEmpty()) {
            rows.emplace(name, keys.join(QStringLiteral(", ")));
        }
    }

    QDialog dialog(window);
    dialog.setWindowTitle(QDialog::tr("Keyboard Shortcuts"));
    dialog.resize(kDialogWidth, kDialogHeight);
    auto* layout = new QVBoxLayout(&dialog);
    auto* filter = new QLineEdit(&dialog);
    filter->setPlaceholderText(QDialog::tr("Filter"));
    filter->setClearButtonEnabled(true);
    layout->addWidget(filter);
    auto* list = new QTreeWidget(&dialog);
    list->setColumnCount(2);
    list->setHeaderLabels({QDialog::tr("Command"), QDialog::tr("Shortcut")});
    list->setRootIsDecorated(false);
    list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (const auto& [name, keys] : rows) {
        list->addTopLevelItem(new QTreeWidgetItem({name, keys}));
    }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);

    QObject::connect(filter, &QLineEdit::textChanged, list, [list](const QString& text) {
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = list->topLevelItem(i);
            item->setHidden(!item->text(0).contains(text, Qt::CaseInsensitive) &&
                            !item->text(1).contains(text, Qt::CaseInsensitive));
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    filter->setFocus();
    dialog.exec();
}

} // namespace kalve137::app
