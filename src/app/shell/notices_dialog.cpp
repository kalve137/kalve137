// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/notices_dialog.h"

#include "app/theme/theme.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace kalve137::app {

namespace {

constexpr int kDialogWidth = theme::kWidthRegular;
constexpr int kDialogHeight = 560;
constexpr const char* kNoticesResource = ":/kalve137/THIRD-PARTY-NOTICES.md";
constexpr const char* kNoticesFolder = "notices";

} // namespace

void showThirdPartyNotices(QWidget* window)
{
    QDialog dialog(window);
    dialog.setWindowTitle(QDialog::tr("Third-Party Notices"));
    dialog.resize(kDialogWidth, kDialogHeight);
    auto* layout = new QVBoxLayout(&dialog);

    auto* text = new QTextBrowser(&dialog);
    text->setObjectName("noticesText");
    text->setOpenLinks(false);
    QFile notices(QString::fromLatin1(kNoticesResource));
    if (notices.open(QIODevice::ReadOnly)) {
        text->setMarkdown(QString::fromUtf8(notices.readAll()));
    }
    QObject::connect(text, &QTextBrowser::anchorClicked, &dialog, [](const QUrl& url) {
        if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")) {
            QDesktopServices::openUrl(url);
        }
    });
    layout->addWidget(text);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    const QDir folder(QApplication::applicationDirPath() + QLatin1Char('/') + QLatin1String(kNoticesFolder));
    if (folder.exists()) {
        QPushButton* open = buttons->addButton(QDialog::tr("Open License Texts"), QDialogButtonBox::ActionRole);
        QObject::connect(open, &QPushButton::clicked, &dialog,
                         [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder.absolutePath())); });
    }
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

} // namespace kalve137::app
