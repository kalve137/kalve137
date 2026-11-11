// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace kalve137::core {

inline constexpr const char* kProjectFileName = "kalve137-project.json";
inline constexpr const char* kExportsFolderName = "exports";

class Project {
public:
    static Project create(const std::filesystem::path& folder, const std::string& name);
    static Project open(const std::filesystem::path& folderOrFile);
    static bool isProjectFolder(const std::filesystem::path& folder);

    const std::filesystem::path& root() const { return m_root; }
    const std::string& name() const { return m_name; }

    std::vector<std::filesystem::path> designs() const;
    std::filesystem::path freeDesignPath(const std::string& base) const;
    std::filesystem::path exportsFolder() const;

private:
    std::filesystem::path m_root;
    std::string m_name;
};

} // namespace kalve137::core
