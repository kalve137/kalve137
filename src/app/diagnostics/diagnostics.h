// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QString>

#include <filesystem>
#include <optional>

namespace kalve137::app::diagnostics {

std::filesystem::path logFolder();
std::filesystem::path crashFolder();

void startLogging();
void stopLogging();
void applyLevels(const QString& preferenceSpec);

void installCrashHandler();

struct Crash {
    std::filesystem::path dump;
    std::filesystem::path log;
    QDateTime time;
};
std::optional<Crash> takePendingCrash();
std::optional<Crash> latestCrash();

void setGlInfo(const QString& info);
QString systemInfo();

} // namespace kalve137::app::diagnostics
