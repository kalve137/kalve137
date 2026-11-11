// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/history.h"

#include "core/serialization.h"

#include <algorithm>
#include <charconv>
#include <exception>
#include <optional>
#include <set>
#include <string>
#include <system_error>

namespace kalve137::core {

namespace {

std::optional<int> versionNumber(const std::filesystem::path& file)
{
    if (file.extension() != kDesignExtension) {
        return std::nullopt;
    }
    const std::string stem = file.stem().string();
    if (stem.size() < 2 || stem.front() != 'v') {
        return std::nullopt;
    }
    int number = 0;
    const auto [end, error] = std::from_chars(stem.data() + 1, stem.data() + stem.size(), number);
    if (error != std::errc() || end != stem.data() + stem.size() || number <= 0) {
        return std::nullopt;
    }
    return number;
}

} // namespace

std::filesystem::path historyFolder(const std::filesystem::path& design)
{
    return design.parent_path() / kHistoryFolderName / design.filename();
}

void archiveVersion(const std::filesystem::path& design, std::size_t keep)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(design, error)) {
        return;
    }
    const std::filesystem::path folder = historyFolder(design);
    std::filesystem::create_directories(folder);
    std::vector<Version> existing = versions(design);
    const int next = existing.empty() ? 1 : existing.front().number + 1;
    std::filesystem::copy_file(design, folder / ("v" + std::to_string(next) + kDesignExtension),
                               std::filesystem::copy_options::overwrite_existing);
    for (std::size_t i = keep > 0 ? keep - 1 : 0; i < existing.size(); ++i) {
        std::filesystem::remove(existing[i].path, error);
    }
}

std::vector<Version> versions(const std::filesystem::path& design)
{
    std::vector<Version> result;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(historyFolder(design), error)) {
        if (!entry.is_regular_file(error)) {
            continue;
        }
        if (const std::optional<int> number = versionNumber(entry.path())) {
            result.push_back({*number, entry.path()});
        }
    }
    std::ranges::sort(result, std::ranges::greater{}, &Version::number);
    return result;
}

void pruneShapes(const std::filesystem::path& design)
{
    std::vector<std::filesystem::path> files{design};
    for (const Version& version : versions(design)) {
        files.push_back(version.path);
    }
    std::set<std::string> used;
    try {
        for (const std::filesystem::path& file : files) {
            used.merge(peekDesign(file).shapes);
        }
    } catch (const std::exception&) {
        return;
    }
    pruneShapeFolder(shapeFolder(design), used);
}

} // namespace kalve137::core
