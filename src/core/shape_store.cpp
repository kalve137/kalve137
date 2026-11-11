// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/shape_store.h"

#include "core/serialization.h"

#include "geometry/exchange.h"

#include <cstdint>
#include <format>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace kalve137::core {

namespace {

constexpr const char* kShapeExtension = ".brep";
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::string keyOf(const std::string& bytes)
{
    std::uint64_t hash = kFnvOffset;
    for (const char byte : bytes) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= kFnvPrime;
    }
    return std::format("{:016x}-{:x}", hash, bytes.size());
}

} // namespace

std::filesystem::path shapeFolder(const std::filesystem::path& design)
{
    return design.parent_path() / kShapeFolderName / design.filename();
}

ShapeStore::ShapeStore(std::filesystem::path folder) : m_folder(std::move(folder)) {}

void ShapeStore::setFolder(std::filesystem::path folder)
{
    m_folder = std::move(folder);
}

std::filesystem::path ShapeStore::pathOf(const std::string& key) const
{
    return m_folder / (key + kShapeExtension);
}

bool ShapeStore::holds(const std::string& key) const
{
    if (m_folder.empty()) {
        return m_memory.contains(key);
    }
    std::error_code error;
    return std::filesystem::is_regular_file(pathOf(key), error);
}

void ShapeStore::write(const std::string& key, const std::string& bytes)
{
    if (m_folder.empty()) {
        m_memory.emplace(key, bytes);
    } else {
        std::filesystem::create_directories(m_folder);
        writeFileAtomically(pathOf(key), bytes);
    }
    ++m_writes;
}

std::string ShapeStore::put(const TopoDS_Shape& shape)
{
    if (const auto known = m_keys.find(shape); known != m_keys.end() && holds(known->second)) {
        return known->second;
    }
    const std::string bytes = geometry::toBinaryBrep(shape);
    std::string key = keyOf(bytes);
    if (!holds(key)) {
        write(key, bytes);
    }
    m_keys[shape] = key;
    return key;
}

TopoDS_Shape ShapeStore::get(const std::string& key)
{
    std::string bytes;
    if (m_folder.empty()) {
        const auto found = m_memory.find(key);
        if (found == m_memory.end()) {
            throw std::runtime_error("The design is missing its stored shape " + key);
        }
        bytes = found->second;
    } else {
        if (!holds(key)) {
            throw std::runtime_error("The design is missing its stored shape " + key);
        }
        bytes = readFile(pathOf(key));
    }
    TopoDS_Shape shape = geometry::fromBinaryBrep(bytes);
    m_keys[shape] = key;
    return shape;
}

void ShapeStore::prune(const std::set<std::string>& keep)
{
    std::erase_if(m_keys, [&](const auto& entry) { return !keep.contains(entry.second); });
    if (m_folder.empty()) {
        std::erase_if(m_memory, [&](const auto& entry) { return !keep.contains(entry.first); });
    } else {
        pruneShapeFolder(m_folder, keep);
    }
}

void pruneShapeFolder(const std::filesystem::path& folder, const std::set<std::string>& keep)
{
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        const std::filesystem::path& file = entry.path();
        if (file.extension() == kShapeExtension && !keep.contains(file.stem().string())) {
            std::filesystem::remove(file, error);
        }
    }
}

} // namespace kalve137::core
