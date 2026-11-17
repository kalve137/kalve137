// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pick/ray_scene.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

namespace kalve137::pick {
namespace {

constexpr double kTolerance = 1e-4;
constexpr double kFar = 1000.0;

void addSquare(Mesh& mesh, double z, std::uint32_t face, double offset = 0.0)
{
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({offset - 1.0, -1.0, z});
    mesh.vertices.push_back({offset + 1.0, -1.0, z});
    mesh.vertices.push_back({offset + 1.0, 1.0, z});
    mesh.vertices.push_back({offset - 1.0, 1.0, z});
    mesh.triangles.push_back({base, base + 1, base + 2});
    mesh.triangles.push_back({base, base + 2, base + 3});
    mesh.faces.insert(mesh.faces.end(), {face, face});
}

TEST(RayScene, FindsTheNearestOfTwoFaces)
{
    Mesh mesh;
    addSquare(mesh, 2.0, 7);
    addSquare(mesh, 5.0, 9);
    const RayScene scene(mesh);
    const std::optional<Hit> hit = scene.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0});
    ASSERT_TRUE(hit);
    EXPECT_NEAR(hit->distance, 2.0, kTolerance);
    EXPECT_EQ(hit->face, 7U);
}

TEST(RayScene, StartsPastTheGivenDistance)
{
    Mesh mesh;
    addSquare(mesh, 2.0, 7);
    addSquare(mesh, 5.0, 9);
    const RayScene scene(mesh);
    const std::optional<Hit> hit = scene.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0}, 3.0);
    ASSERT_TRUE(hit);
    EXPECT_NEAR(hit->distance, 5.0, kTolerance);
    EXPECT_EQ(hit->face, 9U);
}

TEST(RayScene, MissesBesideAndBehind)
{
    Mesh mesh;
    addSquare(mesh, 2.0, 7);
    const RayScene scene(mesh);
    EXPECT_FALSE(scene.first({3.0, 0.0, 0.0}, {0.0, 0.0, 1.0}));
    EXPECT_FALSE(scene.first({0.0, 0.0, 3.0}, {0.0, 0.0, 1.0}));
    EXPECT_TRUE(scene.first({0.0, 0.0, 3.0}, {0.0, 0.0, -1.0}));
}

TEST(RayScene, KeepsPrecisionFarFromTheOrigin)
{
    Mesh mesh;
    addSquare(mesh, kFar + 2.0, 7, kFar);
    addSquare(mesh, kFar + 2.01, 9, kFar);
    const RayScene scene(mesh);
    const std::optional<Hit> hit = scene.first({kFar, 0.0, kFar}, {0.0, 0.0, 2.0}, 2.005);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->face, 9U);
}

TEST(RayScene, EmptyMeshHitsNothing)
{
    const RayScene scene(Mesh{});
    EXPECT_FALSE(scene.first({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}));
}

} // namespace
} // namespace kalve137::pick
