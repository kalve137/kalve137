// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/theme/theme_tokens.h"

#include "app/theme/theme.h"

#include <QRegularExpression>

#include <algorithm>

namespace kalve137::app::theme {

ResolvedSheet resolveTokens(const QString& sheet, const Tokens& tokens)
{
    static const QRegularExpression kToken(QStringLiteral("@([A-Za-z][A-Za-z0-9]*)"));

    ResolvedSheet result;
    qsizetype last = 0;
    for (const QRegularExpressionMatch& match : kToken.globalMatch(sheet)) {
        result.sheet += QStringView(sheet).mid(last, match.capturedStart() - last);
        const QString name = match.captured(1);
        const auto value = tokens.constFind(name);
        if (value != tokens.cend()) {
            result.sheet += *value;
        } else {
            result.sheet += match.captured(0);
            if (!result.unknown.contains(name)) {
                result.unknown.append(name);
            }
        }
        last = match.capturedEnd();
    }
    result.sheet += QStringView(sheet).mid(last);
    return result;
}

Tokens styleTokens(const QString& fontFamily, int lineHeight)
{
    const ChromeColors& colors = palette().chrome;
    const auto color = [](const QColor& value) { return value.name(QColor::HexRgb); };
    const auto px = [](int value) { return QStringLiteral("%1px").arg(value); };
    const auto pt = [](double value) { return QStringLiteral("%1pt").arg(value); };
    const int controlPadding = std::max(0, (kDensity.controlHeight - 2 - lineHeight) / 2);
    const int rowPadding = std::max(0, (kDensity.rowHeight - std::max(lineHeight, kRowIconSize)) / 2);
    const int itemPadding = std::max(0, rowPadding - 1);
    return {
        {QStringLiteral("fontFamily"), QStringLiteral("\"%1\"").arg(fontFamily)},
        {QStringLiteral("textTitle"), pt(kTextTitle)},
        {QStringLiteral("controlPadY"), px(controlPadding)},
        {QStringLiteral("rowPadY"), px(rowPadding)},
        {QStringLiteral("itemPadY"), px(itemPadding)},
        {QStringLiteral("spaceXs"), px(kSpaceXs)},
        {QStringLiteral("spaceS"), px(kSpaceS)},
        {QStringLiteral("spaceM"), px(kSpaceM)},
        {QStringLiteral("spaceL"), px(kSpaceL)},
        {QStringLiteral("spaceXl"), px(kSpaceXl)},
        {QStringLiteral("spaceXxl"), px(kSpaceXxl)},
        {QStringLiteral("radiusSmall"), px(kRadiusSmall)},
        {QStringLiteral("radiusControl"), px(kRadiusControl)},
        {QStringLiteral("radiusPanel"), px(kRadiusPanel)},

        {QStringLiteral("base"), color(colors.base)},
        {QStringLiteral("inputDisabled"), color(colors.inputDisabled)},
        {QStringLiteral("surface"), color(colors.surface)},
        {QStringLiteral("borderDisabled"), color(colors.borderDisabled)},
        {QStringLiteral("raisedPressed"), color(colors.raisedPressed)},
        {QStringLiteral("surfaceHover"), color(colors.surfaceHover)},
        {QStringLiteral("raised"), color(colors.raised)},
        {QStringLiteral("raisedHover"), color(colors.raisedHover)},
        {QStringLiteral("border"), color(colors.border)},
        {QStringLiteral("borderHover"), color(colors.borderHover)},
        {QStringLiteral("text"), color(colors.text)},
        {QStringLiteral("textBright"), color(colors.textBright)},
        {QStringLiteral("textDim"), color(colors.textDim)},
        {QStringLiteral("textDisabled"), color(colors.textDisabled)},
        {QStringLiteral("accent"), color(colors.accent)},
        {QStringLiteral("accentHover"), color(colors.accentHover)},
        {QStringLiteral("conflict"), color(colors.conflict)},
        {QStringLiteral("conflictText"), color(colors.conflictText)},
        {QStringLiteral("warning"), color(colors.warning)},
    };
}

} // namespace kalve137::app::theme
