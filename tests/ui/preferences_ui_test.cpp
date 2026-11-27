// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include "app/session/preferences.h"
#include "app/theme/theme.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QPalette>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSettings>
#include <QWidget>

#include <gp_Pnt.hxx>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

namespace kalve137::uitest {
namespace {

using PreferencesTest = UiTest;

constexpr int kCubeMargin = 40;
constexpr QPointF kCubeEdgeOffset(0.0, 0.3);
constexpr QPointF kCubeFaceOffset(-0.33, 0.15);
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

QComboBox* themeCombo(QWidget& dialog)
{
    for (QComboBox* combo : dialog.findChildren<QComboBox*>()) {
        if (combo->findText(QStringLiteral("Light")) >= 0) {
            return combo;
        }
    }
    return nullptr;
}

void chooseTheme(QWidget& dialog, const QString& name, bool keep)
{
    QComboBox* combo = themeCombo(dialog);
    ASSERT_NE(combo, nullptr);
    combo->setCurrentIndex(combo->findText(name));
    EXPECT_EQ(QApplication::palette().color(QPalette::Window), app::theme::kLightPalette.chrome.surface)
        << "theme switches while the dialog is open";
    auto* box = qobject_cast<QDialog*>(&dialog);
    ASSERT_NE(box, nullptr);
    if (keep) {
        box->accept();
    } else {
        box->reject();
    }
}

TEST_F(PreferencesTest, ChoosingLightSwitchesLiveAndIsKept)
{
    answerDialog([](QWidget& dialog) { chooseTheme(dialog, QStringLiteral("Light"), true); });
    ui->trigger("Preferences...");
    EXPECT_FALSE(app::theme::isDark());
    EXPECT_EQ(app::Preferences::load().colorScheme, app::theme::Scheme::Light);
    EXPECT_TRUE(qApp->styleSheet().contains(QStringLiteral("qss/light/")));
    ui->save("theme-light");

    app::theme::setScheme(app::theme::Scheme::Dark);
    EXPECT_TRUE(app::theme::isDark());
    EXPECT_EQ(QApplication::palette().color(QPalette::Window), app::theme::kDarkPalette.chrome.surface);
}

TEST_F(PreferencesTest, CancelingTheDialogRestoresTheTheme)
{
    answerDialog([](QWidget& dialog) { chooseTheme(dialog, QStringLiteral("Light"), false); });
    ui->trigger("Preferences...");
    EXPECT_TRUE(app::theme::isDark());
    EXPECT_EQ(app::Preferences::load().colorScheme, app::theme::Scheme::Dark);
    EXPECT_EQ(QApplication::palette().color(QPalette::Window), app::theme::kDarkPalette.chrome.surface);
}

TEST_F(PreferencesTest, LightThemeShowsABodyASketchAndADialog)
{
    app::Preferences preferences = app::Preferences::load();
    preferences.colorScheme = app::theme::Scheme::Light;
    preferences.save();
    ui = std::make_unique<UiDriver>();
    ui->trigger("Open Sample");
    const std::filesystem::path folder = ui->save("light body").parent_path();
    const bool oldView = ui->viewport() == nullptr;
    const auto saveCube = [&](const QRectF& cube, const std::string& name) {
        const QRect area = cube.toAlignedRect().adjusted(-kCubeMargin, -kCubeMargin, kCubeMargin, kCubeMargin);
        const QPoint corner = ui->view().mapTo(ui->view().window(), area.topLeft());
        ui->shot().copy(QRect(corner, area.size())).save(QString::fromStdU16String((folder / name).u16string()));
    };
    const auto saveCubeHovers = [&](const std::string& theme) {
        if (!oldView) {
            return;
        }
        const QRectF cube = ui->view().viewCubeRect();
        saveCube(cube, theme + " view cube.png");
        const auto hover = [&](const QPointF& offset, const std::string& part) {
            ui->move(cube.center() + QPointF(offset.x() * cube.width(), offset.y() * cube.height()));
            saveCube(cube, theme + " view cube " + part + ".png");
        };
        hover(QPointF(), "hover");
        hover(kCubeEdgeOffset, "edge");
        hover(kCubeFaceOffset, "face");
    };
    saveCubeHovers("light");
    ui->move(ui->centerOf(ui->canvas()));

    ui->trigger("Create Sketch");
    ui->click(ui->originPlane(core::OriginPlane::XY));
    ui->trigger("Rectangle");
    ui->click(gp_Pnt(-20, -10, 0));
    ui->click(gp_Pnt(20, 10, 0));
    ui->trigger("Dimension");
    ui->click(gp_Pnt(0, -10, 0));
    ui->click(gp_Pnt(5, -14, 0));
    ui->enterValue("50");
    ui->save("light sketch");
    ui->trigger("Finish Sketch");

    answerDialog([folder](QWidget& dialog) {
        dialog.grab().save(QString::fromStdU16String((folder / "light preferences.png").u16string()));
        if (auto* box = qobject_cast<QDialog*>(&dialog)) {
            box->reject();
        }
    });
    ui->trigger("Preferences...");
    EXPECT_FALSE(app::theme::isDark());

    app::theme::setScheme(app::theme::Scheme::Dark);
    ui->move(ui->centerOf(ui->canvas()));
    saveCubeHovers("dark");
}

TEST_F(PreferencesTest, AnUnknownMouseSchemeFallsBackToTheDefault)
{
    QSettings().setValue("preferences/navigation/mouseScheme", "no-such-scheme");
    EXPECT_EQ(app::Preferences::load().navigation.preset, app::Preferences().navigation.preset);
}

} // namespace
} // namespace kalve137::uitest
