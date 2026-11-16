// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kernel/shapes.h"
#include "lang/parse.h"
#include "model/json.h"
#include "model/select.h"
#include "rebuild/rebuild.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace kalve137;
namespace fs = std::filesystem;

struct Example {
    std::string name;
    std::string source;
};

std::vector<Example> examples()
{
    std::vector<Example> out;
    for (const auto& entry : fs::directory_iterator(fs::path(KALVE137_SOURCE_DIR) / "examples")) {
        if (entry.path().extension() != ".kd") {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        out.push_back({entry.path().filename().string(), text.str()});
    }
    std::sort(out.begin(), out.end(), [](const Example& a, const Example& b) { return a.name < b.name; });
    return out;
}

std::set<std::string> names(const model::Snapshot& snapshot)
{
    std::set<std::string> out;
    for (const model::Face& face : snapshot.faces) {
        out.insert(face.name);
    }
    return out;
}

bool failedLoudly(const model::Snapshot& snapshot)
{
    return std::any_of(snapshot.diagnostics.begin(), snapshot.diagnostics.end(),
                       [](const lang::Diagnostic& d) { return d.severity == lang::Diagnostic::Severity::Error; });
}

std::string difference(const std::set<std::string>& from, const std::set<std::string>& to)
{
    std::string text;
    for (const std::string& name : from) {
        if (!to.contains(name)) {
            text += " -" + name;
        }
    }
    for (const std::string& name : to) {
        if (!from.contains(name)) {
            text += " +" + name;
        }
    }
    return text;
}

TEST(ParamSweepTest, EveryPointKeepsItsNamesOrFailsLoudly)
{
    constexpr double kFactors[] = {0.5, 0.75, 0.9, 1.1, 1.25, 1.5, 2.0};
    const std::vector<Example> all = examples();
    ASSERT_GE(all.size(), 3U);
    for (const Example& example : all) {
        const model::Snapshot written = rebuild::build(example.source, {false, {}});
        const bool stops = failedLoudly(written);
        const std::set<std::string> expected = names(written);
        int same = 0;
        int loud = 0;
        std::map<std::string, int> codes;
        for (const auto& [param, value] : written.params) {
            if (value.thread || !(value.dim == lang::Dim{1, 0}) || value.value <= 0.0) {
                continue;
            }
            for (const double factor : kFactors) {
                const model::Snapshot swept = rebuild::build(example.source, {false, {{param, value.value * factor}}});
                if (failedLoudly(swept)) {
                    ++loud;
                    for (const lang::Diagnostic& d : swept.diagnostics) {
                        if (d.severity == lang::Diagnostic::Severity::Error) {
                            ++codes[d.code];
                            break;
                        }
                    }
                    continue;
                }
                EXPECT_FALSE(stops) << example.name << " stops as written but built with " << param << " x" << factor;
                const std::set<std::string> got = names(swept);
                EXPECT_EQ(got, expected) << example.name << " with " << param << " x" << factor
                                         << " built with other names and no error:" << difference(expected, got);
                ++same;
            }
        }
        std::cout << "sweep " << example.name << ": " << same << " same names, " << loud << " failed loudly (";
        for (const auto& [code, count] : codes) {
            std::cout << code << " " << count << (code == codes.rbegin()->first ? "" : ", ");
        }
        std::cout << ")\n";
    }
}

TEST(NamesResolveTest, EveryPrintedSelectorFindsItsFace)
{
    for (const Example& example : examples()) {
        const model::Snapshot snapshot = rebuild::build(example.source);
        if (failedLoudly(snapshot)) {
            continue;
        }
        const nlohmann::json json = model::toJson(snapshot, false);
        ASSERT_EQ(json.at("faces").size(), snapshot.faces.size()) << example.name;
        for (std::size_t i = 0; i < snapshot.faces.size(); ++i) {
            const std::string line = model::describeFace(snapshot, static_cast<int>(i));
            const std::string printed[] = {line.substr(0, line.find(": ")),
                                           json.at("faces")[i].at("selector").get<std::string>()};
            for (const std::string& selector : printed) {
                const lang::ExpressionResult parsed = lang::parseExpression(selector);
                ASSERT_NE(parsed.expr, nullptr) << example.name << ": " << selector;
                const model::Selection found = model::select(*parsed.expr, snapshot);
                EXPECT_EQ(found.items, std::vector<int>{static_cast<int>(i)}) << example.name << ": " << selector;
            }
        }
        std::cout << "names " << example.name << ": " << snapshot.faces.size() << " faces resolve\n";
    }
}

TEST(StepRoundTripTest, EveryExampleComesBackTheSame)
{
    const fs::path dir = fs::temp_directory_path() / "kalve137_step_round_trip";
    fs::create_directories(dir);
    for (const Example& example : examples()) {
        const rebuild::Build built = rebuild::run(example.source, {false, {}});
        ASSERT_FALSE(built.shape.empty()) << example.name;
        const kernel::Inspection before = kernel::inspect(built.shape, {});
        const fs::path path = dir / (example.name + ".step");
        rebuild::exportStep(example.source, path);
        const std::vector<kernel::Shape> read = kernel::importStep(path);
        ASSERT_EQ(read.size(), 1U) << example.name;
        const kernel::Inspection after = kernel::inspect(read.front(), {});
        EXPECT_EQ(after.solids, before.solids) << example.name;
        EXPECT_EQ(after.faces.size(), before.faces.size()) << example.name;
        EXPECT_NEAR(after.volume, before.volume, 1e-6 * before.volume) << example.name;
        std::cout << "step " << example.name << ": " << after.solids << " solid, " << after.faces.size() << " faces, "
                  << after.volume << " mm3 back, " << before.volume << " mm3 out\n";
    }
    fs::remove_all(dir);
}

} // namespace
