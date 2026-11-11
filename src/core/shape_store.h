// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <TopoDS_Shape.hxx>

#include <cstddef>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

namespace kalve137::core {

inline constexpr const char* kShapeFolderName = ".shapes";

std::filesystem::path shapeFolder(const std::filesystem::path& design);

class ShapeStore {
public:
    ShapeStore() = default;
    explicit ShapeStore(std::filesystem::path folder);

    const std::filesystem::path& folder() const { return m_folder; }
    void setFolder(std::filesystem::path folder);

    std::string put(const TopoDS_Shape& shape);
    TopoDS_Shape get(const std::string& key);

    void prune(const std::set<std::string>& keep);

    std::size_t writes() const { return m_writes; }

private:
    std::filesystem::path pathOf(const std::string& key) const;
    bool holds(const std::string& key) const;
    void write(const std::string& key, const std::string& bytes);

    std::filesystem::path m_folder;
    std::unordered_map<TopoDS_Shape, std::string> m_keys;
    std::map<std::string, std::string> m_memory;
    std::size_t m_writes = 0;
};

void pruneShapeFolder(const std::filesystem::path& folder, const std::set<std::string>& keep);

} // namespace kalve137::core
