// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/theme/theme.h"
#include "app/theme/theme_tokens.h"

#include <gtest/gtest.h>

#include <QColor>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace {

using kalve137::app::theme::ChromeColors;
using kalve137::app::theme::kDarkPalette;
using kalve137::app::theme::kLightPalette;
using kalve137::app::theme::Palette;
using kalve137::app::theme::resolveTokens;
using kalve137::app::theme::styleTokens;
using kalve137::app::theme::Tokens;
using kalve137::app::theme::ViewportColors;
using Role = QColor ChromeColors::*;

constexpr double kText = 4.5;
constexpr double kGraphic = 3.0;

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

double luminance(const QColor& color)
{
    const auto channel = [](double value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

double contrast(const QColor& a, const QColor& b)
{
    const double first = luminance(a);
    const double second = luminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

struct Pair {
    const char* foreground;
    QColor front;
    std::vector<std::pair<const char*, QColor>> backgrounds;
    double floor;
};

void expectFloors(const Palette& palette)
{
    const ChromeColors& c = palette.chrome;
    const ViewportColors& v = palette.viewport;
    const std::vector<Pair> pairs = {
        {"text",
         c.text,
         {{"base", c.base}, {"surface", c.surface}, {"raised", c.raised}, {"raisedHover", c.raisedHover}},
         kText},
        {"textDim", c.textDim, {{"base", c.base}, {"surface", c.surface}}, kText},
        {"textDisabled", c.textDisabled, {{"surface", c.surface}}, kGraphic},
        {"accent", c.accent, {{"base", c.base}, {"surface", c.surface}, {"raised", c.raised}}, kText},
        {"onAccent", c.onAccent, {{"accent", c.accent}, {"accentHover", c.accentHover}}, kText},
        {"onAccentFill", c.onAccentFill, {{"accentFill", c.accentFill}, {"accentFillHover", c.accentFillHover}}, kText},
        {"conflictText", c.conflictText, {{"base", c.base}, {"surface", c.surface}, {"raised", c.raised}}, kText},
        {"warning", c.warning, {{"base", c.base}, {"surface", c.surface}, {"raised", c.raised}}, kText},
        {"conflict", c.conflict, {{"canvas", v.canvas}, {"surface", c.surface}}, kGraphic},
        {"accent", c.accent, {{"canvas", v.canvas}}, kGraphic},
        {"cubeLabel", v.cubeLabel, {{"raised", c.raised}}, kText},
        {"wire", v.wire, {{"canvas", v.canvas}}, kGraphic},
        {"bodyEdge", v.bodyEdge, {{"body", v.body}}, kGraphic},
        {"sketchFree", v.sketchFree, {{"canvas", v.canvas}, {"halo", v.halo}}, kGraphic},
        {"sketchConstrained", v.sketchConstrained, {{"canvas", v.canvas}, {"halo", v.halo}}, kGraphic},
        {"sketchConstruction", v.sketchConstruction, {{"canvas", v.canvas}, {"halo", v.halo}}, kGraphic},
        {"annotation", v.annotation, {{"canvas", v.canvas}, {"halo", v.halo}}, kText},
        {"gridLabel", v.gridLabel, {{"canvas", v.canvas}}, kText},
    };
    for (const Pair& pair : pairs) {
        for (const auto& [name, back] : pair.backgrounds) {
            EXPECT_GE(contrast(pair.front, back), pair.floor) << pair.foreground << " on " << name;
        }
    }
}

TEST(PaletteContrast, DarkMeetsFloors)
{
    expectFloors(kDarkPalette);
}

TEST(PaletteContrast, LightMeetsFloors)
{
    expectFloors(kLightPalette);
}

TEST(ThemeStyleSheet, IconsMatchTheirPalette)
{
    const std::vector<std::pair<QString, Role>> icons = {
        {"check", &ChromeColors::accent},
        {"partial", &ChromeColors::accent},
        {"check_disabled", &ChromeColors::textDisabled},
        {"partial_disabled", &ChromeColors::textDisabled},
        {"chevron_down", &ChromeColors::textDim},
        {"chevron_up", &ChromeColors::textDim},
    };
    static const QRegularExpression kStroke(QStringLiteral("stroke=\"(#[0-9A-Fa-f]{6})\""));
    for (const auto& [scheme, palette] :
         {std::pair{QStringLiteral("dark"), &kDarkPalette}, std::pair{QStringLiteral("light"), &kLightPalette}}) {
        for (const auto& [name, role] : icons) {
            QFile file(QStringLiteral(KALVE137_SOURCE_DIR "/assets/icons/qss/%1/%2.svg").arg(scheme, name));
            ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text)) << file.fileName().toStdString();
            const QRegularExpressionMatch match = kStroke.match(QString::fromUtf8(file.readAll()));
            ASSERT_TRUE(match.hasMatch()) << file.fileName().toStdString();
            EXPECT_EQ(QColor(match.captured(1)), palette->chrome.*role) << file.fileName().toStdString();
        }
    }
}

} // namespace
