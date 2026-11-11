// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/project.h"

#include "core/serialization.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace kalve137::core {

namespace {

constexpr int kProjectFormatVersion = 1;

std::filesystem::path fromUtf8(const std::string& text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace

Project Project::create(const std::filesystem::path& folder, const std::string& name)
{
    std::filesystem::create_directories(folder);
    const nlohmann::json root = {{"format", "kalve137-project"}, {"version", kProjectFormatVersion}, {"name", name}};
    writeFileAtomically(folder / kProjectFileName, root.dump(2) + "\n");
    std::filesystem::create_directories(folder / kExportsFolderName);
    return open(folder);
}

Project Project::open(const std::filesystem::path& folderOrFile)
{
    const std::filesystem::path folder =
        folderOrFile.filename() == kProjectFileName ? folderOrFile.parent_path() : folderOrFile;
    if (!isProjectFolder(folder)) {
        throw std::runtime_error("The folder is not a Kalve 137 project");
    }
    Project project;
    project.m_root = std::filesystem::absolute(folder);
    try {
        const nlohmann::json root = nlohmann::json::parse(readFile(folder / kProjectFileName));
        if (root.value("format", std::string()) != "kalve137-project") {
            throw std::runtime_error("The folder is not a Kalve 137 project");
        }
        const std::u8string fallback = project.m_root.filename().u8string();
        project.m_name = root.value("name", std::string(fallback.begin(), fallback.end()));
    } catch (const nlohmann::json::exception&) {
        throw std::runtime_error("The project file is damaged");
    }
    return project;
}

bool Project::isProjectFolder(const std::filesystem::path& folder)
{
    std::error_code error;
    return std::filesystem::is_regular_file(folder / kProjectFileName, error);
}

std::vector<std::filesystem::path> Project::designs() const
{
    std::vector<std::filesystem::path> result;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(m_root, error)) {
        if (entry.is_regular_file() && entry.path().extension() == kDesignExtension) {
            result.push_back(entry.path());
        }
    }
    std::ranges::sort(result);
    return result;
}

std::filesystem::path Project::freeDesignPath(const std::string& base) const
{
    std::filesystem::path candidate = m_root / fromUtf8(base + kDesignExtension);
    for (int i = 2; std::filesystem::exists(candidate); ++i) {
        candidate = m_root / fromUtf8(base + " " + std::to_string(i) + kDesignExtension);
    }
    return candidate;
}

std::filesystem::path Project::exportsFolder() const
{
    const std::filesystem::path folder = m_root / kExportsFolderName;
    std::filesystem::create_directories(folder);
    return folder;
}

} // namespace kalve137::core
