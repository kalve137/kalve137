// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace kalve137::app {

inline constexpr const char* kImportBatchFlag = "--import-batch";
inline constexpr const char* kImportOneFlag = "--import-one";
inline constexpr int kImportTimeoutSeconds = 600;

int runImportBatch(const std::filesystem::path& input, const std::filesystem::path& out, int timeoutSeconds);
int runImportOne(const std::filesystem::path& step, const std::filesystem::path& work);

} // namespace kalve137::app
