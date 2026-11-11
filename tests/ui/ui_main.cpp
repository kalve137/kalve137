// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crash_trace.h"
#include "ui_driver.h"

#include "app/theme/theme.h"
#include "log/log.h"

#include <OcctQtTools.h>

#include <QApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    kalve137::uitest::installCrashTrace();
    testing::InitGoogleTest(&argc, argv);
    OcctQtTools::qtGlPlatformSetup();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QApplication::setApplicationName("Kalve 137");
    QApplication::setOrganizationName("Kalve 137 UI Tests");
    QApplication::setApplicationVersion(KALVE137_VERSION);
    kalve137::uitest::installHarness();
    kalve137::app::theme::apply(app);
    const int result = RUN_ALL_TESTS();
    kalve137::uitest::resetSettings();
    kalve137::log::shutdown();
    return result;
}
