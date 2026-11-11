// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/document.h"
#include "core/shape_store.h"

#include <cstddef>
#include <filesystem>
#include <set>
#include <string>

namespace kalve137::core {

inline constexpr int kDesignFormatVersion = 7;
inline constexpr const char* kDesignExtension = ".k137";

std::string toJson(const Document& document, ShapeStore& shapes);
Document fromJson(const std::string& text, ShapeStore& shapes);

void saveDesign(const Document& document, const std::filesystem::path& path, ShapeStore& shapes);
Document loadDesign(const std::filesystem::path& path, ShapeStore& shapes);

struct DesignInfo {
    std::size_t features = 0;
    std::set<std::string> shapes;
};
DesignInfo peekDesign(const std::filesystem::path& path);

void writeFileAtomically(const std::filesystem::path& path, const std::string& contents);
std::string readFile(const std::filesystem::path& path);

} // namespace kalve137::core
