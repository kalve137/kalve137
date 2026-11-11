// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"
#include "core/recompute.h"
#include "core/serialization.h"
#include "core/shape_store.h"
#include "geometry/shapes.h"

#include <gtest/gtest.h>

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>

using namespace kalve137;
using namespace kalve137::core;

namespace {

constexpr double kInradius = 20.0;
constexpr double kPrismRadius = 40.0;
constexpr double kSlabHalfWidth = 100.0;
constexpr int kSides = 5;
constexpr double kDegrees = 180.0 / std::numbers::pi;
const geometry::Color kSteel{0x64 / 255.0, 0x7D / 255.0, 0x9E / 255.0};
const geometry::Color kGold{0xF2 / 255.0, 0xB7 / 255.0, 0x05 / 255.0};

const std::filesystem::path kSamplePath =
    std::filesystem::path(KALVE137_SOURCE_DIR) / "assets" / "samples" / "dodecahedron.k137";

double edgeLength()
{
    return 2.0 * kInradius / std::sqrt(2.5 + 5.5 / std::sqrt(5.0));
}

double dodecahedronVolume()
{
    return (15.0 + 7.0 * std::sqrt(5.0)) / 4.0 * std::pow(edgeLength(), 3);
}

sketch::Sketch polygon(int sides, double radius, double startDegrees)
{
    sketch::Sketch s;
    std::vector<sketch::Id> corners;
    for (int i = 0; i < sides; ++i) {
        const double angle = (startDegrees + 360.0 * i / sides) / kDegrees;
        corners.push_back(s.addPoint({radius * std::cos(angle), radius * std::sin(angle)}));
    }
    for (int i = 0; i < sides; ++i) {
        s.addLine(corners[i], corners[(i + 1) % sides]);
    }
    return s;
}

Document dodecahedron()
{
    Document doc;
    SketchFeature base;
    base.sketch = polygon(kSides, kPrismRadius, 90.0);
    const FeatureId baseSketch = doc.add(base);
    ExtrudeFeature prism;
    prism.profiles = {baseSketch, {{0.0, 0.0}}};
    prism.extent = ExtrudeExtent::Symmetric;
    prism.distance = 2.0 * kInradius;
    prism.bodyName = "Dodecahedron";
    const FeatureId body = doc.add(prism);

    const double tilt = std::atan(2.0) * kDegrees;
    for (int i = 0; i < kSides; ++i) {
        const double azimuth = (-90.0 + 360.0 * i / kSides) / kDegrees;
        PlaneFeature plane;
        plane.kind = PlaneKind::Angle;
        plane.base = originPlane(OriginPlane::XY);
        plane.baseName = "XY";
        plane.hinge = gp_Ax1(gp::Origin(), gp_Dir(-std::sin(azimuth), std::cos(azimuth), 0.0));
        plane.angleDegrees = tilt;
        const FeatureId planeId = doc.add(plane);

        SketchFeature slab;
        slab.plane = constructionPlane(plane).frame;
        slab.planeName = "Plane" + std::to_string(i + 1);
        slab.planeFeature = planeId;
        slab.sketch = polygon(4, kSlabHalfWidth * std::numbers::sqrt2, 45.0);
        const FeatureId slabSketch = doc.add(slab);

        ExtrudeFeature cut;
        cut.profiles = {slabSketch, {{0.0, 0.0}}};
        cut.extent = ExtrudeExtent::Symmetric;
        cut.distance = 2.0 * kInradius;
        cut.operation = Operation::Intersect;
        doc.add(cut);
    }

    doc.add(AppearanceFeature{{{body, 0}}, {}, kSteel});
    doc.add(AppearanceFeature{{}, {gp_Pnt(0.0, 0.0, kInradius)}, kGold});
    return doc;
}

int faceCount(const TopoDS_Shape& shape)
{
    int count = 0;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        ++count;
    }
    return count;
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

TEST(Sample, DodecahedronBuildsFromSixSlabs)
{
    const Model model = recompute(dodecahedron());
    for (const auto& [id, status] : model.status) {
        EXPECT_EQ(status.health, FeatureHealth::Ok) << id << ": " << status.message;
    }
    ASSERT_EQ(model.bodies.size(), 1U);
    const Body& body = model.bodies.front();
    EXPECT_EQ(faceCount(body.shape), 12);
    EXPECT_NEAR(geometry::volume(body.shape), dodecahedronVolume(), 1e-6 * dodecahedronVolume());
    ASSERT_TRUE(body.color);
    EXPECT_NEAR(body.color->blue, kSteel.blue, 1e-9);
    EXPECT_EQ(body.faceColors.size(), 1U);
}

TEST(Sample, ShippedDodecahedronMatchesItsRecipe)
{
    ShapeStore shapes;
    const std::string expected = toJson(dodecahedron(), shapes);
    if (const char* update = std::getenv("KALVE137_UPDATE_SAMPLES"); update != nullptr && std::string(update) == "1") {
        std::filesystem::create_directories(kSamplePath.parent_path());
        std::ofstream(kSamplePath, std::ios::binary) << expected;
    }
    ASSERT_TRUE(std::filesystem::exists(kSamplePath)) << "Run with KALVE137_UPDATE_SAMPLES=1 to write it";
    EXPECT_EQ(readText(kSamplePath), expected) << "Run with KALVE137_UPDATE_SAMPLES=1 to refresh it";
    const Model model = recompute(fromJson(readText(kSamplePath), shapes));
    ASSERT_EQ(model.bodies.size(), 1U);
    EXPECT_NEAR(geometry::volume(model.bodies.front().shape), dodecahedronVolume(), 1e-6 * dodecahedronVolume());
}
