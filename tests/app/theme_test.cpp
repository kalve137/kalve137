// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/theme/theme_tokens.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QRegularExpression>

namespace {

using kalve137::app::theme::resolveTokens;
using kalve137::app::theme::styleTokens;
using kalve137::app::theme::Tokens;

QString readStyleSheet()
{
    QFile file(QStringLiteral(KALVE137_SOURCE_DIR "/assets/theme/theme.qss"));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text)) << "cannot open " << file.fileName().toStdString();
    return QString::fromUtf8(file.readAll());
}

TEST(ThemeTokens, ReplacesWholeNamesOnly)
{
    const Tokens tokens{{"surface", "#111111"}, {"surfaceHover", "#222222"}};
    const auto resolved = resolveTokens("a: @surface; b: @surfaceHover;", tokens);
    EXPECT_EQ(resolved.sheet, "a: #111111; b: #222222;");
    EXPECT_TRUE(resolved.unknown.isEmpty());
}

TEST(ThemeTokens, ReportsUnknownNamesOnceAndKeepsThem)
{
    const Tokens tokens{{"surface", "#111111"}};
    const auto resolved = resolveTokens("@missing @surfaceX @missing", tokens);
    EXPECT_EQ(resolved.sheet, "@missing @surfaceX @missing");
    EXPECT_EQ(resolved.unknown, QStringList({"missing", "surfaceX"}));
}

TEST(ThemeTokens, LeavesTextWithoutTokensAlone)
{
    const QString sheet = "QToolButton[popupMode=\"1\"] { padding-right: 18px; }";
    EXPECT_EQ(resolveTokens(sheet, styleTokens("Roboto", 14)).sheet, sheet);
}

TEST(ThemeStyleSheet, HasNoHexColors)
{
    const QString sheet = readStyleSheet();
    static const QRegularExpression kHex(QStringLiteral("#[0-9A-Fa-f]{3,8}\\b"));
    for (const QRegularExpressionMatch& match : kHex.globalMatch(sheet)) {
        ADD_FAILURE() << "theme.qss has hex color " << match.captured(0).toStdString()
                      << ", use a token from theme_tokens.cpp";
    }
}

TEST(ThemeStyleSheet, UsesOnlyKnownTokens)
{
    const auto resolved = resolveTokens(readStyleSheet(), styleTokens("Roboto", 14));
    EXPECT_TRUE(resolved.unknown.isEmpty()) << "unknown tokens: " << resolved.unknown.join(", ").toStdString();
}

} // namespace
