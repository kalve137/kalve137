// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/startup_options.h"

#include <gtest/gtest.h>

namespace kalve137::app {
namespace {

const QString kProgram = QStringLiteral("kalve137");

TEST(StartupOptions, DefaultsToTheOldView)
{
    const ParsedStartup parsed = parseStartup({kProgram, QStringLiteral("design.k137")}, {});
    EXPECT_TRUE(parsed.error.isEmpty());
    EXPECT_EQ(parsed.options.view, ViewKind::Old);
    EXPECT_FALSE(parsed.options.graphicsApi);
    EXPECT_EQ(parsed.arguments, QStringList({kProgram, QStringLiteral("design.k137")}));
}

TEST(StartupOptions, StripsTheFlagsFromTheArguments)
{
    const ParsedStartup parsed =
        parseStartup({kProgram, QStringLiteral("--view"), QStringLiteral("new"), QStringLiteral("--graphics-api"),
                      QStringLiteral("Vulkan"), QStringLiteral("design.k137")},
                     {});
    EXPECT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    EXPECT_EQ(parsed.options.view, ViewKind::New);
    EXPECT_EQ(parsed.options.graphicsApi, render::GraphicsApi::Vulkan);
    EXPECT_EQ(parsed.arguments, QStringList({kProgram, QStringLiteral("design.k137")}));
}

TEST(StartupOptions, TheVariableSelectsTheViewAndTheFlagWins)
{
    EXPECT_EQ(parseStartup({kProgram}, QStringLiteral("new")).options.view, ViewKind::New);
    EXPECT_EQ(
        parseStartup({kProgram, QStringLiteral("--view"), QStringLiteral("old")}, QStringLiteral("new")).options.view,
        ViewKind::Old);
}

TEST(StartupOptions, RejectsUnknownValues)
{
    EXPECT_FALSE(parseStartup({kProgram, QStringLiteral("--view"), QStringLiteral("fast")}, {}).error.isEmpty());
    EXPECT_FALSE(parseStartup({kProgram}, QStringLiteral("fast")).error.isEmpty());
    EXPECT_FALSE(parseStartup({kProgram, QStringLiteral("--view")}, {}).error.isEmpty());
    EXPECT_FALSE(parseStartup({kProgram, QStringLiteral("--view"), QStringLiteral("new"),
                               QStringLiteral("--graphics-api"), QStringLiteral("glide")},
                              {})
                     .error.isEmpty());
}

TEST(StartupOptions, TheApiOverrideNeedsTheNewView)
{
    EXPECT_FALSE(
        parseStartup({kProgram, QStringLiteral("--graphics-api"), QStringLiteral("d3d12")}, {}).error.isEmpty());
}

} // namespace
} // namespace kalve137::app
