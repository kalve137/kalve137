// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/shell/startup_options.h"

#include <QString>

#include <filesystem>

namespace kalve137::app {

inline constexpr const char* kBenchOrbitFlag = "--bench-orbit";
inline constexpr const char* kBenchChangeFlag = "--bench-change";

int runBenchOrbit(const StartupOptions& options, const std::filesystem::path& design, const std::filesystem::path& out,
                  const QString& size);
int runBenchChange(const StartupOptions& options, const std::filesystem::path& design, const std::filesystem::path& out,
                   const QString& size);

} // namespace kalve137::app
