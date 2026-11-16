// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "kernel/shapes.h"
#include "model/snapshot.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kalve137::rebuild {

struct Options {
    bool meshes = true;
    std::map<std::string, double> overrides;
};

struct Stage {
    std::string statement;
    kernel::Shape shape;
    std::vector<std::string> names;
};

struct Build {
    model::Snapshot snapshot;
    std::vector<Stage> stages;
    kernel::Shape shape;
};

Build run(const std::string& source, const Options& options = {});

model::Snapshot build(const std::string& source, const Options& options = {});

model::Snapshot at(const Build& build, const std::string& statement, bool meshes);

void exportStep(const std::string& source, const std::filesystem::path& path);

} // namespace kalve137::rebuild
