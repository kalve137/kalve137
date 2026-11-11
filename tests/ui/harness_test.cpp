// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "app/shell/main_window.h"
#include "viewer/occt_view_widget.h"

#include <QAbstractButton>
#include <QImage>
#include <QMessageBox>
#include <QTextBrowser>

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

TEST_F(HarnessTest, AboutOpensTheThirdPartyNotices)
{
    QString shown;
    answerDialog([](QWidget& widget) {
        auto* box = qobject_cast<QMessageBox*>(&widget);
        ASSERT_NE(box, nullptr) << widget.metaObject()->className();
        for (QAbstractButton* button : box->buttons()) {
            if (button->text() == QStringLiteral("Third-Party Notices")) {
                button->click();
                return;
            }
        }
        ADD_FAILURE() << "No Third-Party Notices button";
        box->close();
    });
    answerDialog([&shown](QWidget& widget) {
        if (const auto* text = widget.findChild<QTextBrowser*>("noticesText")) {
            shown = text->toPlainText();
        }
        widget.close();
    });
    ui->trigger("About Kalve 137");
    EXPECT_EQ(pendingAnswers(), 0U);
    EXPECT_TRUE(shown.contains("Open CASCADE Technology")) << shown.toStdString();
    EXPECT_TRUE(shown.contains("LGPL-3.0-only")) << shown.toStdString();
}

} // namespace
} // namespace kalve137::uitest
