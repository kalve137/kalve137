// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class QWidget;

namespace kalve137::app {

void showShortcuts(QWidget* window);

QString commandName(QString text);

} // namespace kalve137::app
