// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"
#include "core/history.h"
#include "core/recompute.h"
#include "core/serialization.h"
#include "core/shape_store.h"
#include "geometry/exchange.h"
#include "geometry/shapes.h"

#include <gtest/gtest.h>

#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace kalve137;
using namespace kalve137::core;

constexpr int kManyParts = 50;
constexpr std::size_t kBytesPerPart = 200;

double totalVolume(const Model& model)
{
    double sum = 0.0;
    for (const Body& body : model.bodies) {
        sum += geometry::volume(body.shape);
    }
    return sum;
}

Document importOf(int count)
{
    std::vector<geometry::StepPart> parts;
    for (int i = 0; i < count; ++i) {
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(20.0 * i, 0.0, 0.0));
        parts.push_back({"Box " + std::to_string(i), geometry::makeBox(10.0, 10.0, 10.0).Moved(shift), std::nullopt});
    }
    Document doc;
    doc.add(ImportFeature{"boxes.step", std::move(parts)});
    return doc;
}

std::size_t fileCount(const std::filesystem::path& folder)
{
    std::size_t count = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        count += entry.is_regular_file() ? 1 : 0;
    }
    return count;
}

class ShapeStoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_folder = std::filesystem::temp_directory_path() / "kalve137-shape-store-test";
        std::filesystem::remove_all(m_folder);
        std::filesystem::create_directories(m_folder);
        m_design = m_folder / "assembly.k137";
    }

    void TearDown() override { std::filesystem::remove_all(m_folder); }

    std::filesystem::path m_folder;
    std::filesystem::path m_design;
};

TEST_F(ShapeStoreTest, RoundTripsAShape)
{
    ShapeStore shapes(shapeFolder(m_design));
    const std::string key = shapes.put(geometry::makeBox(10.0, 20.0, 30.0));
    EXPECT_TRUE(std::filesystem::is_regular_file(shapeFolder(m_design) / (key + ".brep")));

    ShapeStore fresh(shapeFolder(m_design));
    EXPECT_NEAR(geometry::volume(fresh.get(key)), 6000.0, 1e-6);
    EXPECT_THROW(fresh.get("missing"), std::runtime_error);
}

TEST_F(ShapeStoreTest, SavingAgainWritesNoShapes)
{
    const Document doc = importOf(3);
    ShapeStore shapes(shapeFolder(m_design));
    saveDesign(doc, m_design, shapes);
    EXPECT_EQ(shapes.writes(), 3u);
    saveDesign(doc, m_design, shapes);
    EXPECT_EQ(shapes.writes(), 3u);

    ShapeStore reopened(shapeFolder(m_design));
    const Document loaded = loadDesign(m_design, reopened);
    saveDesign(loaded, m_design, reopened);
    EXPECT_EQ(reopened.writes(), 0u);
    EXPECT_NEAR(totalVolume(recompute(loaded)), 3000.0, 1e-6);
}

TEST_F(ShapeStoreTest, NewFolderGetsTheShapesItLacks)
{
    const Document doc = importOf(2);
    ShapeStore shapes(shapeFolder(m_design));
    saveDesign(doc, m_design, shapes);
    const std::filesystem::path copy = m_folder / "copy.k137";
    shapes.setFolder(shapeFolder(copy));
    saveDesign(doc, copy, shapes);
    EXPECT_EQ(fileCount(shapeFolder(copy)), 2u);

    ShapeStore reopened(shapeFolder(copy));
    EXPECT_NEAR(totalVolume(recompute(loadDesign(copy, reopened))), 2000.0, 1e-6);
}

TEST_F(ShapeStoreTest, ManyPartsKeepTheDesignSmall)
{
    ShapeStore shapes(shapeFolder(m_design));
    saveDesign(importOf(kManyParts), m_design, shapes);
    EXPECT_LT(std::filesystem::file_size(m_design), kManyParts * kBytesPerPart);
    EXPECT_EQ(peekDesign(m_design).shapes.size(), static_cast<std::size_t>(kManyParts));
}

TEST_F(ShapeStoreTest, VersionTwoInlineShapesLoad)
{
    using nlohmann::json;
    const json part = {{"name", "Block"}, {"brep", geometry::toBrep(geometry::makeBox(10.0, 20.0, 30.0))}};
    const json data = {{"source", "block.step"}, {"parts", json::array({part})}};
    const json feature = {{"id", 1}, {"type", "import"}, {"name", "Import1"}, {"data", data}};
    const json legacy = {{"format", "kalve137-design"},
                         {"version", 2},
                         {"marker", 1},
                         {"next_id", 2},
                         {"features", json::array({feature})}};
    writeFileAtomically(m_design, legacy.dump());

    ShapeStore shapes(shapeFolder(m_design));
    const Document loaded = loadDesign(m_design, shapes);
    EXPECT_NEAR(totalVolume(recompute(loaded)), 6000.0, 1e-6);
    saveDesign(loaded, m_design, shapes);
    EXPECT_EQ(peekDesign(m_design).shapes.size(), 1u);
}

TEST_F(ShapeStoreTest, PruningKeepsShapesVersionsUse)
{
    ShapeStore shapes(shapeFolder(m_design));
    Document doc = importOf(2);
    saveDesign(doc, m_design, shapes);
    const std::set<std::string> first = peekDesign(m_design).shapes;

    archiveVersion(m_design);
    doc.remove(doc.features().front().id);
    doc.add(ImportFeature{"other.step", {{"Pin", geometry::makeCylinder(gp_Pnt(), 2.0, 10.0), std::nullopt}}});
    saveDesign(doc, m_design, shapes);
    std::filesystem::path orphan = shapeFolder(m_design) / "0000000000000000-0.brep";
    writeFileAtomically(orphan, "unused");

    pruneShapes(m_design);
    EXPECT_FALSE(std::filesystem::exists(orphan));
    EXPECT_EQ(fileCount(shapeFolder(m_design)), 3u);
    ShapeStore reopened(shapeFolder(m_design));
    for (const std::string& key : first) {
        EXPECT_NO_THROW(reopened.get(key));
    }

    std::filesystem::remove_all(historyFolder(m_design));
    pruneShapes(m_design);
    EXPECT_EQ(fileCount(shapeFolder(m_design)), 1u);
}

std::uintmax_t folderSize(const std::filesystem::path& folder)
{
    std::uintmax_t size = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        size += entry.is_regular_file() ? entry.file_size() : 0;
    }
    return size;
}

TEST_F(ShapeStoreTest, DISABLED_LargeImportSave)
{
    char* value = nullptr;
    std::size_t length = 0;
#ifdef _WIN32
    _dupenv_s(&value, &length, "KALVE137_BENCH_STEP");
    const std::string step = value != nullptr ? value : "";
    std::free(value);
#else
    value = std::getenv("KALVE137_BENCH_STEP");
    const std::string step = value != nullptr ? value : "";
#endif
    if (step.empty()) {
        GTEST_SKIP() << "KALVE137_BENCH_STEP is not set";
    }
    using Clock = std::chrono::steady_clock;
    const auto seconds = [](Clock::time_point since) {
        return std::chrono::duration<double>(Clock::now() - since).count();
    };

    Clock::time_point start = Clock::now();
    Document doc;
    doc.add(ImportFeature{"bench.step", geometry::readStepParts(step)});
    const std::size_t parts = std::get<ImportFeature>(doc.features().front().data).parts.size();
    std::cout << "read " << parts << " parts in " << seconds(start) << " s\n";

    ShapeStore shapes(shapeFolder(m_design));
    start = Clock::now();
    saveDesign(doc, m_design, shapes);
    std::cout << "first save " << seconds(start) << " s, design " << std::filesystem::file_size(m_design)
              << " bytes, shapes " << folderSize(shapeFolder(m_design)) << " bytes\n";

    doc.find(doc.features().front().id)->name = "Renamed";
    start = Clock::now();
    saveDesign(doc, m_design, shapes);
    std::cout << "second save " << seconds(start) << " s\n";

    start = Clock::now();
    Document copy = doc;
    std::cout << "document copy " << seconds(start) << " s\n";

    ShapeStore fresh(shapeFolder(m_design));
    start = Clock::now();
    const Document loaded = loadDesign(m_design, fresh);
    std::cout << "load " << seconds(start) << " s\n";
    EXPECT_EQ(std::get<ImportFeature>(loaded.features().front().data).parts.size(), parts);
    EXPECT_EQ(copy.features().size(), doc.features().size());
}

} // namespace
