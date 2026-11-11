// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace kalve137::log {

struct ZipEntry {
    std::string name;
    std::filesystem::path source;
    std::string contents;
};

void writeZip(const std::filesystem::path& path, const std::vector<ZipEntry>& entries);

std::vector<std::string> zipEntries(const std::filesystem::path& path);

} // namespace kalve137::log
