// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "app/shell/main_window.h"
#include "app/theme/theme.h"
#include "viewer/occt_view_widget.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QStatusTipEvent>
#include <QStringList>
#include <QTextBrowser>
#include <QWindow>

#include <set>

namespace kalve137::uitest {
namespace {

using HarnessTest = UiTest;

TEST_F(HarnessTest, RendersTheWindowOffScreen)
{
    EXPECT_TRUE(ui->window().testAttribute(Qt::WA_DontShowOnScreen));
    const QImage view = ui->view().grabFramebuffer();
    ASSERT_FALSE(view.isNull());
    std::set<QRgb> colors;
    for (int y = 0; y < view.height(); y += 16) {
        for (int x = 0; x < view.width(); x += 16) {
            colors.insert(view.pixel(x, y));
        }
    }
    EXPECT_GT(colors.size(), 3U) << "The view rendered a flat image";
    const QImage whole = ui->shot();
    EXPECT_EQ(whole.size(), ui->window().size() * ui->window().devicePixelRatio());
    ui->save("start");
}

TEST_F(HarnessTest, FindsEveryOriginPlane)
{
    ui->trigger("Create Sketch");
    for (const auto plane : {core::OriginPlane::XY, core::OriginPlane::XZ, core::OriginPlane::YZ}) {
        ui->originPlane(plane);
        EXPECT_TRUE(ui->view().gridPlane().Direction().IsParallel(core::originPlane(plane).Direction(), 1e-6));
    }
}

TEST_F(HarnessTest, StateListsAnEmptyDesign)
{
    const nlohmann::json state = ui->state();
    EXPECT_TRUE(state["features"].empty());
    EXPECT_TRUE(state["bodies"].empty());
    EXPECT_EQ(state["sketch"], 0);
}

TEST_F(HarnessTest, StatusHintComesBackAfterAMenuTip)
{
    const QString hint = ui->statusText();
    ASSERT_FALSE(hint.isEmpty());
    QStatusTipEvent tip(QStringLiteral("Menu item tip"));
    QCoreApplication::sendEvent(&ui->window(), &tip);
    EXPECT_EQ(ui->statusText(), QStringLiteral("Menu item tip"));
    QStatusTipEvent cleared{QString()};
    QCoreApplication::sendEvent(&ui->window(), &cleared);
    EXPECT_EQ(ui->statusText(), hint);
}

TEST_F(HarnessTest, ReportDialogStatesMissingPartsAsNotes)
{
    QStringList notes;
    answerDialog([&notes](QWidget& widget) {
        for (const QCheckBox* box : widget.findChildren<QCheckBox*>()) {
            EXPECT_TRUE(box->isEnabled()) << box->text().toStdString();
        }
        for (const QLabel* label : widget.findChildren<QLabel*>("panelHint")) {
            notes << label->text();
        }
        for (QPushButton* button : widget.findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("Save Zip...")) {
                EXPECT_TRUE(paintsAccent(*button)) << "Save Zip lost its primary style";
            }
        }
        widget.close();
    });
    ui->trigger("Report a Problem...");
    EXPECT_TRUE(notes.contains(QStringLiteral("Save design to include it"))) << notes.join(", ").toStdString();
}

TEST_F(HarnessTest, AboutOpensTheThirdPartyNotices)
{
    QString shown;
    QString copied;
    int noticesWidth = 0;
    answerDialog([&copied](QWidget& widget) {
        EXPECT_EQ(widget.objectName(), QStringLiteral("aboutDialog"));
        ASSERT_NE(widget.windowHandle(), nullptr);
        const Qt::WindowFlags flags = widget.windowHandle()->flags();
        EXPECT_TRUE(flags.testFlag(Qt::CustomizeWindowHint)) << "Windows adds minimize and maximize without it";
        EXPECT_FALSE(flags.testAnyFlags(Qt::WindowMinMaxButtonsHint));
        const auto* version = widget.findChild<QLabel*>("aboutVersion");
        ASSERT_NE(version, nullptr);
        EXPECT_TRUE(version->text().startsWith(QStringLiteral("Version "))) << version->text().toStdString();
        for (const QLabel* label : widget.findChildren<QLabel*>()) {
            EXPECT_FALSE(label->text().contains(QStringLiteral("GLvendor")))
                << "Graphics details belong in Copy Details";
        }
        QAbstractButton* notices = nullptr;
        for (QAbstractButton* button : widget.findChildren<QAbstractButton*>()) {
            if (button->text() == QStringLiteral("Copy Details")) {
                QClipboard* clipboard = QGuiApplication::clipboard();
                const QString before = clipboard->text();
                button->click();
                copied = clipboard->text();
                clipboard->setText(before);
            } else if (button->text() == QStringLiteral("Third-Party Notices")) {
                notices = button;
            } else if (button->text() == QStringLiteral("Close")) {
                EXPECT_TRUE(paintsAccent(*button));
            }
        }
        ASSERT_NE(notices, nullptr) << "No Third-Party Notices button";
        notices->click();
        widget.close();
    });
    answerDialog([&shown, &noticesWidth](QWidget& widget) {
        if (const auto* text = widget.findChild<QTextBrowser*>("noticesText")) {
            shown = text->toPlainText();
        }
        noticesWidth = widget.width();
        EXPECT_LT(widget.maximumWidth(), QWIDGETSIZE_MAX) << "Windows adds maximize to an unbounded window";
        widget.close();
    });
    ui->trigger("About Kalve 137");
    EXPECT_EQ(pendingAnswers(), 0U);
    EXPECT_EQ(noticesWidth, app::theme::kWidthWide);
    EXPECT_TRUE(copied.startsWith(QStringLiteral("Version: "))) << copied.toStdString();
    EXPECT_TRUE(shown.contains("Open CASCADE Technology")) << shown.toStdString();
    EXPECT_TRUE(shown.contains("LGPL-3.0-only")) << shown.toStdString();
}

} // namespace
} // namespace kalve137::uitest
