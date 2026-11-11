// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace kalve137::app {

inline constexpr const char* kSmokeFlag = "--smoke";
inline constexpr const char* kSmokeOrganization = "Kalve 137 Smoke";

void prepareSmoke();
int runSmoke(const std::filesystem::path& design, const std::filesystem::path& out);

} // namespace kalve137::app
