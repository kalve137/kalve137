// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace kalve137::core {

inline constexpr const char* kHistoryFolderName = ".history";
inline constexpr std::size_t kKeptVersions = 50;

struct Version {
    int number = 0;
    std::filesystem::path path;
};

std::filesystem::path historyFolder(const std::filesystem::path& design);

void archiveVersion(const std::filesystem::path& design, std::size_t keep = kKeptVersions);

std::vector<Version> versions(const std::filesystem::path& design);

void pruneShapes(const std::filesystem::path& design);

} // namespace kalve137::core
