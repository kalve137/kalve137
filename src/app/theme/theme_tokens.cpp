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

        {QStringLiteral("base"), color(kSlateDeep)},
        {QStringLiteral("inputDisabled"), color(kSlateDisabled)},
        {QStringLiteral("surface"), color(kSlate)},
        {QStringLiteral("borderDisabled"), color(kSlateEdgeDisabled)},
        {QStringLiteral("raisedPressed"), color(kSlatePressed)},
        {QStringLiteral("surfaceHover"), color(kSlateHover)},
        {QStringLiteral("raised"), color(kSlateLight)},
        {QStringLiteral("raisedHover"), color(kSlateLightHover)},
        {QStringLiteral("border"), color(kSlateEdge)},
        {QStringLiteral("borderHover"), color(kSlateEdgeHover)},
        {QStringLiteral("text"), color(kTitaniumLight)},
        {QStringLiteral("textBright"), color(kTitaniumBright)},
        {QStringLiteral("textDim"), color(kTitanium)},
        {QStringLiteral("textDisabled"), color(kTitaniumDim)},
        {QStringLiteral("accent"), color(kGold)},
        {QStringLiteral("accentHover"), color(kGoldLight)},
        {QStringLiteral("conflict"), color(kConflict)},
        {QStringLiteral("conflictText"), color(kConflictText)},
        {QStringLiteral("warning"), color(kWarning)},
    };
}

} // namespace kalve137::app::theme
