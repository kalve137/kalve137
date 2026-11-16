// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lang/ast.h"
#include "model/snapshot.h"

#include <nlohmann/json.hpp>

#include <vector>

namespace kalve137::model {

nlohmann::json toJson(const lang::Diagnostic& diagnostic);
nlohmann::json toJson(const std::vector<lang::Diagnostic>& diagnostics);
nlohmann::json toJson(const Snapshot& snapshot, bool meshes);

} // namespace kalve137::model
