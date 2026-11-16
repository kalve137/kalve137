// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "app/session/preferences.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QPointF>
#include <QSettings>
#include <QWidget>

#include <gp_Pnt.hxx>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>

namespace kalve137::uitest {
namespace {

using PreferencesTest = UiTest;
using viewer::NavigationPreset;

TEST_F(PreferencesTest, EveryMouseSchemeSurvivesASaveAndLoad)
{
    for (const NavigationPreset preset :
         {NavigationPreset::MiddleOrbitsCtrlPans, NavigationPreset::MiddlePansShiftOrbits,
          NavigationPreset::MiddleOrbitsShiftPans, NavigationPreset::RightOrbitsCtrlPans}) {
        app::Preferences preferences;
        preferences.navigation.preset = preset;
        preferences.save();
        EXPECT_EQ(app::Preferences::load().navigation.preset, preset);
    }
}

QWidget* openMarkingMenu()
{
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->isVisible() && widget->inherits("kalve137::app::MarkingMenu")) {
            return widget;
        }
    }
    return nullptr;
}

std::array<QPointF, 3> viewMarks(const UiDriver& ui)
{
    return {ui.at(gp_Pnt(0, 0, 0)), ui.at(gp_Pnt(10, 0, 0)), ui.at(gp_Pnt(0, 0, 10))};
}

double offset(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

class RightOrbitTest : public PreferencesTest {
protected:
    void SetUp() override
    {
        PreferencesTest::SetUp();
        app::Preferences preferences = app::Preferences::load();
        preferences.navigation.preset = NavigationPreset::RightOrbitsCtrlPans;
        preferences.save();
        ui = std::make_unique<UiDriver>();
        ui->settle();
        m_center = ui->centerOf(&ui->view());
    }

    QPointF m_center;
};

TEST_F(RightOrbitTest, MiddleClickOpensTheMarkingMenu)
{
    ui->click(m_center, Qt::MiddleButton);
    QWidget* menu = openMarkingMenu();
    ASSERT_NE(menu, nullptr);
    menu->close();
    ui->settle();
}

TEST_F(RightOrbitTest, RightDragOrbitsWithoutAMenu)
{
    const std::array<QPointF, 3> before = viewMarks(*ui);
    ui->drag(m_center, m_center + QPointF(80, 30), Qt::RightButton);
    EXPECT_EQ(openMarkingMenu(), nullptr);
    const std::array<QPointF, 3> after = viewMarks(*ui);
    const QPointF axisBefore = before[1] - before[0];
    const QPointF axisAfter = after[1] - after[0];
    EXPECT_GT(offset(axisBefore, axisAfter), 2.0) << "X axis turns on screen";
}

TEST_F(RightOrbitTest, CtrlRightDragPans)
{
    const std::array<QPointF, 3> before = viewMarks(*ui);
    ui->drag(m_center, m_center + QPointF(80, 30), Qt::RightButton, Qt::ControlModifier);
    EXPECT_EQ(openMarkingMenu(), nullptr);
    const std::array<QPointF, 3> after = viewMarks(*ui);
    EXPECT_GT(offset(before[0], after[0]), 20.0) << "Origin moves on screen";
    EXPECT_LT(offset(before[1] - before[0], after[1] - after[0]), 1.0) << "Axes keep their direction";
    EXPECT_LT(offset(before[2] - before[0], after[2] - after[0]), 1.0) << "Axes keep their direction";
}

TEST_F(PreferencesTest, AnUnknownMouseSchemeFallsBackToTheDefault)
{
    QSettings().setValue("preferences/navigation/mouseScheme", "no-such-scheme");
    EXPECT_EQ(app::Preferences::load().navigation.preset, app::Preferences().navigation.preset);
}

} // namespace
} // namespace kalve137::uitest
