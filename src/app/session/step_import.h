// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/feature.h"
#include "geometry/exchange.h"

#include <filesystem>
#include <vector>

namespace kalve137::app {

core::ImportFeature importFeature(const std::filesystem::path& path, std::vector<geometry::StepPart> parts);

} // namespace kalve137::app
