// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include "app/feature/recent_values.h"
#include "viewer/occt_view_widget.h"

#include <QAction>
#include <QLineEdit>

#include <gtest/gtest.h>

#include <vector>

namespace kalve137::uitest {
namespace {

using RecentValuesTest = UiTest;

constexpr double kNear = 1e-6;

QAction* recentAction(const QWidget* box)
{
    for (QAction* action : box->actions()) {
        if (action->objectName() == "recentValues") {
            return action;
        }
    }
    return nullptr;
}

QLineEdit* handleBox(UiDriver& ui)
{
    for (QLineEdit* edit : ui.view().findChildren<QLineEdit*>("canvasInput")) {
        if (edit->isVisible()) {
            return edit;
        }
    }
    return nullptr;
}

TEST_F(RecentValuesTest, DimensionBoxOffersTypedValuesNewestFirst)
{
    sketchRectangle(*ui, core::OriginPlane::XY, gp_Pnt(-20, -10, 0), gp_Pnt(20, 10, 0));
    ui->doubleClick(ui->timelineChip(1));
    ui->trigger("Circle");
    ui->click(gp_Pnt(0, 0, 0));
    ui->click(gp_Pnt(3, 0, 0));
    ui->trigger("Dimension");
    ui->click(gp_Pnt(0, -10, 0));
    ui->click(gp_Pnt(10, -13, 0));
    const QAction* action = recentAction(ui->focused());
    ASSERT_NE(action, nullptr);
    EXPECT_FALSE(action->isVisible()) << "Nothing typed yet";
    ui->key(Qt::Key_Return);
    EXPECT_TRUE(app::recentValues(core::Quantity::Length).empty()) << "Accepting the measured value adds nothing";

    ui->click(gp_Pnt(-20, 0, 0));
    ui->click(gp_Pnt(-14, 4, 0));
    ui->enterValue("20");
    ui->click(gp_Pnt(3, 0, 0));
    ui->click(gp_Pnt(6, 6, 0));
    ui->enterValue("8");
    EXPECT_EQ(app::recentValues(core::Quantity::Length), (std::vector<double>{8.0, 20.0}));

    ui->click(gp_Pnt(6, 6, 0));
    action = recentAction(ui->focused());
    ASSERT_NE(action, nullptr);
    EXPECT_TRUE(action->isVisible());
    ui->save("recent values offered");
    chooseFromMenu("20 mm");
    ui->key(Qt::Key_Down, Qt::AltModifier);
    ASSERT_NE(ui->focused(), nullptr);
    EXPECT_EQ(ui->focused()->objectName(), "dimensionEditor") << "Choosing keeps the box open";
    ui->key(Qt::Key_Return);
    const nlohmann::json state = ui->state();
    ASSERT_EQ(state["dimensions"].size(), 3U) << state.dump(2);
    EXPECT_NEAR(state["dimensions"][2]["value"].get<double>(), 20.0, kNear) << state.dump(2);
    EXPECT_EQ(app::recentValues(core::Quantity::Length), (std::vector<double>{20.0, 8.0}));
    EXPECT_TRUE(app::recentValues(core::Quantity::Angle).empty());
}

TEST_F(RecentValuesTest, HandleBoxRemembersTypedDistance)
{
    sketchRectangle(*ui, core::OriginPlane::XY, gp_Pnt(-20, -10, 0), gp_Pnt(20, 10, 0));
    ui->trigger("Extrude");
    ASSERT_TRUE(ui->state()["featurePanel"]) << ui->state().dump(2);
    QLineEdit* box = handleBox(*ui);
    ASSERT_NE(box, nullptr);
    ASSERT_NE(recentAction(box), nullptr);
    EXPECT_FALSE(recentAction(box)->isVisible());
    ui->click(box);
    ui->enterValue("17");
    EXPECT_EQ(app::recentValues(core::Quantity::Length), (std::vector<double>{17.0}));
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    ASSERT_EQ(ui->document().features().size(), 2U);
    ui->trigger("Undo");
    ASSERT_EQ(ui->document().features().size(), 1U);

    ui->trigger("Extrude");
    box = handleBox(*ui);
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(recentAction(box)->isVisible());
    ui->click(box);
    ui->save("handle box offers recent values");
    chooseFromMenu("17 mm");
    ui->key(Qt::Key_Down, Qt::AltModifier);
    EXPECT_EQ(box->text(), QStringLiteral("17 mm"));
}

} // namespace
} // namespace kalve137::uitest
