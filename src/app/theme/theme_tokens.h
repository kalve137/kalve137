// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace kalve137::app::theme {

using Tokens = QHash<QString, QString>;

struct ResolvedSheet {
    QString sheet;
    QStringList unknown;
};

ResolvedSheet resolveTokens(const QString& sheet, const Tokens& tokens);

Tokens styleTokens(const QString& fontFamily, int lineHeight);

} // namespace kalve137::app::theme
