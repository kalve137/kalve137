// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "look/raster.h"
#include "model/snapshot.h"
#include "rebuild/rebuild.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace kalve137;

double medianMs(int runs, const std::function<void()>& work)
{
    std::vector<double> times;
    for (int i = 0; i < runs; ++i) {
        const auto started = std::chrono::steady_clock::now();
        work();
        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

std::string example(const std::string& name)
{
    std::ifstream in(std::filesystem::path(KALVE137_SOURCE_DIR) / "examples" / name, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    std::string source = text.str();
    source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
    return source;
}

model::Snapshot torus(int rings, int segments)
{
    model::Snapshot snapshot;
    snapshot.built = true;
    model::Face face;
    face.kind = "torus";
    face.name = "ring.round";
    face.selector = face.name;
    const double major = 40.0;
    const double minor = 12.0;
    for (int i = 0; i < rings; ++i) {
        const double a = 2.0 * std::numbers::pi * i / rings;
        for (int j = 0; j < segments; ++j) {
            const double b = 2.0 * std::numbers::pi * j / segments;
            face.vertices.push_back({(major + minor * std::cos(b)) * std::cos(a),
                                     (major + minor * std::cos(b)) * std::sin(a), minor * std::sin(b)});
        }
    }
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segments; ++j) {
            const int p = i * segments + j;
            const int q = ((i + 1) % rings) * segments + j;
            const int r = ((i + 1) % rings) * segments + (j + 1) % segments;
            const int s = i * segments + (j + 1) % segments;
            face.triangles.push_back({p, q, r});
            face.triangles.push_back({p, r, s});
        }
    }
    snapshot.boxMin = {-major - minor, -major - minor, -minor};
    snapshot.boxMax = {major + minor, major + minor, minor};
    snapshot.faces.push_back(std::move(face));
    return snapshot;
}

std::size_t triangles(const model::Snapshot& snapshot)
{
    std::size_t count = 0;
    for (const model::Face& face : snapshot.faces) {
        count += face.triangles.size();
    }
    return count;
}

std::string plate(int pairs)
{
    std::ostringstream out;
    out << "part plate\n\nparam thick = 6 mm\n\nsketch base on XY {\n  rect outline center (0 mm, 0 mm) size ("
        << pairs * 20 + 20 << " mm, 40 mm)\n}\n\nextrude body = base up thick\n";
    for (int i = 1; i <= pairs; ++i) {
        const int x = -pairs * 10 + (i - 1) * 20 + 10;
        out << "\nsketch h" << i << " on body.outline.end {\n  circle a center (" << x
            << " mm, 8 mm) diameter 4 mm\n  circle b center (" << x << " mm, -8 mm) diameter 4 mm\n}\n\ncut c" << i
            << " = h" << i << " depth 3 mm\n";
    }
    out << "\ncheck light: volume() > 0 mm3\n";
    return out.str();
}

} // namespace

int main()
{
    const model::Snapshot bracket = rebuild::build(example("bracket.kd"), {true, {}});
    const model::Snapshot ring = torus(250, 200);
    const std::vector<std::pair<int, int>> sizes = {{1280, 720}, {1920, 1080}, {3840, 2160}};
    std::cout << "render, median of 7 runs, one call to look::render without supersampling\n";
    std::cout << "part, triangles, size, ms\n";
    for (const auto& [name, snapshot] : {std::pair<std::string, const model::Snapshot*>{"bracket", &bracket},
                                         std::pair<std::string, const model::Snapshot*>{"torus", &ring}}) {
        for (const auto& [width, height] : sizes) {
            const double ms = medianMs(7, [&] { look::render(*snapshot, {}, width, height); });
            std::cout << name << ", " << triangles(*snapshot) << ", " << width << "x" << height << ", " << ms << "\n";
        }
    }
    std::cout << "\nrebuild without meshes, median of 5 runs by total\n";
    std::cout << "statements, total ms, inspect ms, name ms, other ms, faces at the end\n";
    for (const int pairs : {2, 10, 23}) {
        const std::string source = plate(pairs);
        std::vector<model::Snapshot> runs;
        runs.reserve(5);
        for (int i = 0; i < 5; ++i) {
            runs.push_back(rebuild::build(source, {false, {}}));
        }
        std::sort(runs.begin(), runs.end(),
                  [](const auto& a, const auto& b) { return a.totalMilliseconds < b.totalMilliseconds; });
        const model::Snapshot& median = runs[runs.size() / 2];
        double inspect = 0.0;
        double name = 0.0;
        for (const model::Timing& timing : median.timings) {
            inspect += timing.inspectMilliseconds;
            name += timing.nameMilliseconds;
        }
        std::cout << 5 + 2 * pairs << ", " << median.totalMilliseconds << ", " << inspect << ", " << name << ", "
                  << median.totalMilliseconds - inspect - name << ", " << median.faces.size()
                  << (lang::hasErrors(median.diagnostics) ? ", errors" : "") << "\n";
    }
    const model::Snapshot largest = rebuild::build(plate(23), {false, {}});
    std::cout << "\nper statement in the 51 statement file, statement, ms, inspect ms, name ms\n";
    for (const model::Timing& timing : largest.timings) {
        std::cout << timing.statement << ", " << timing.milliseconds << ", " << timing.inspectMilliseconds << ", "
                  << timing.nameMilliseconds << "\n";
    }
    std::cout << "total, " << largest.totalMilliseconds << "\n";

    const std::string tab = example("tab.kd");
    std::string free = tab;
    free.replace(free.find("  point a\n"), 10, "  point a near (0 mm, 0 mm)\n");
    free.erase(free.find("  at(a) = (0 mm, 0 mm)\n"), 23);
    std::cout << "\nsketch statement of tab.kd, median of 21 runs, sketch, ms\n";
    for (const auto& [label, source] : {std::pair<std::string, const std::string*>{"pinned with probe", &tab},
                                        std::pair<std::string, const std::string*>{"free without probe", &free}}) {
        std::vector<double> times;
        for (int i = 0; i < 21; ++i) {
            const model::Snapshot snapshot = rebuild::build(*source, {false, {}});
            for (const model::Timing& timing : snapshot.timings) {
                if (timing.statement == "outline") {
                    times.push_back(timing.milliseconds);
                }
            }
        }
        std::sort(times.begin(), times.end());
        std::cout << label << ", " << (times.empty() ? -1.0 : times[times.size() / 2]) << "\n";
    }
    return 0;
}
