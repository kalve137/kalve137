// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "app/session/preferences.h"

#include <QSettings>

#include <gtest/gtest.h>

namespace kalve137::uitest {
namespace {

using PreferencesTest = UiTest;
using viewer::NavigationPreset;

TEST_F(PreferencesTest, EveryMouseSchemeSurvivesASaveAndLoad)
{
    for (const NavigationPreset preset :
         {NavigationPreset::MiddleOrbitsCtrlPans, NavigationPreset::MiddlePansShiftOrbits,
          NavigationPreset::MiddleOrbitsShiftPans}) {
        app::Preferences preferences;
        preferences.navigation.preset = preset;
        preferences.save();
        EXPECT_EQ(app::Preferences::load().navigation.preset, preset);
    }
}

TEST_F(PreferencesTest, AnUnknownMouseSchemeFallsBackToTheDefault)
{
    QSettings().setValue("preferences/navigation/mouseScheme", "no-such-scheme");
    EXPECT_EQ(app::Preferences::load().navigation.preset, app::Preferences().navigation.preset);
}

} // namespace
} // namespace kalve137::uitest
