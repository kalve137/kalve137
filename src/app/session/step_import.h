// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/feature.h"
#include "geometry/exchange.h"

#include "snapshot/body.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace kalve137::app {

struct ImportedStep {
    std::vector<geometry::StepPart> parts;
    std::vector<std::shared_ptr<const snapshot::Mesh>> meshes;
};

core::ImportFeature importFeature(const std::filesystem::path& path, std::vector<geometry::StepPart> parts);

ImportedStep readStepForImport(const std::filesystem::path& path, const geometry::ProgressCallback& progress = {});

} // namespace kalve137::app
