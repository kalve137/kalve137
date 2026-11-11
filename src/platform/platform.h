// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace kalve137::platform {

std::string_view name();

void writeDebugger(std::string_view text);

std::uint64_t totalMemory();

struct CrashOptions {
    std::filesystem::path folder;
    std::string prefix;
    std::function<void(const std::filesystem::path& base)> onCrash;
};

void installCrashHandler(CrashOptions options);

[[noreturn]] void crashForTesting();

} // namespace kalve137::platform
