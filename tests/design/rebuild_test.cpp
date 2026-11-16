// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rebuild/rebuild.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace {

using namespace kalve137;

std::string example(const std::string& name)
{
    std::ifstream in(std::filesystem::path(KALVE137_SOURCE_DIR) / "examples" / name, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    std::string source = text.str();
    source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
    return source;
}

TEST(RebuildCostTest, TheTableIsBuiltOnlyWhenAStatementReadsIt)
{
    const model::Snapshot snapshot = rebuild::build(example("bracket.kd"), {false, {}});
    std::map<std::string, bool> inspected;
    for (const model::Timing& timing : snapshot.timings) {
        inspected[timing.statement] = timing.inspectMilliseconds > 0.0;
    }
    for (const char* reads : {"post", "holes", "mounts", "soft", "wall", "final table"}) {
        EXPECT_TRUE(inspected.at(reads)) << reads << " reads a new body and built no table";
    }
    for (const char* skips : {"base", "body", "riser", "tapped", "bolts", "web", "light", "tall", "blind"}) {
        EXPECT_FALSE(inspected.at(skips)) << skips << " built a table it does not need";
    }
}

TEST(RebuildCostTest, ViewsAtAStatementAreFullyMeasured)
{
    const rebuild::Build built = rebuild::run(example("bracket.kd"), {false, {}});
    const model::Snapshot holes = rebuild::at(built, "holes", false);
    EXPECT_GT(holes.volume, 0.0);
    for (const model::Face& face : holes.faces) {
        EXPECT_GT(face.area, 0.0) << face.selector;
    }
    for (const model::Edge& edge : holes.edges) {
        EXPECT_GT(edge.length, 0.0) << edge.selector;
    }
}

} // namespace
