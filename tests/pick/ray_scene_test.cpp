// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pick/ray_scene.h"
#include "pick/scene_rays.h"
#include "snapshot/body.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace kalve137::pick {
namespace {

using snapshot::Mesh;

constexpr double kTolerance = 1e-4;
constexpr double kFar = 1000.0;

void addSquare(Mesh& mesh, double z, std::uint32_t face, double offset = 0.0)
{
    mesh.faceCount = std::max(mesh.faceCount, face + 1);
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

TEST(RayScene, NumbersFacesOnAcrossMeshes)
{
    Mesh near;
    addSquare(near, 2.0, 2);
    Mesh far;
    addSquare(far, 5.0, 1);
    const RayScene scene(std::vector<const Mesh*>{&near, &far});
    const std::optional<Hit> first = scene.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0});
    ASSERT_TRUE(first);
    EXPECT_EQ(first->face, 2U);
    const std::optional<Hit> second = scene.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0}, 3.0);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->face, near.faceCount + 1U);
}

TEST(RayScene, PassesHiddenMeshes)
{
    Mesh near;
    addSquare(near, 2.0, 0);
    Mesh far;
    addSquare(far, 5.0, 0);
    const RayScene scene(std::vector<const Mesh*>{&near, &far});
    const std::optional<Hit> both = scene.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0});
    ASSERT_TRUE(both);
    EXPECT_EQ(both->mesh, 0U);

    const std::vector<bool> farOnly{false, true};
    const std::optional<Hit> passed = scene.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0}, 0.0, &farOnly);
    ASSERT_TRUE(passed);
    EXPECT_EQ(passed->mesh, 1U);
    EXPECT_NEAR(passed->distance, 5.0, kTolerance);
    EXPECT_EQ(passed->face, near.faceCount);

    const std::vector<bool> none{false, false};
    EXPECT_FALSE(scene.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0}, 0.0, &none));
    const std::vector<bool> nearOnly{true, false};
    EXPECT_FALSE(scene.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0}, 3.0, &nearOnly));
}

TEST(RayScene, PassesAHiddenMeshFarFromTheRayStart)
{
    Mesh near;
    addSquare(near, kFar * kFar, 0);
    Mesh far;
    addSquare(far, kFar * kFar + 10.0, 0);
    const RayScene scene(std::vector<const Mesh*>{&near, &far});
    const std::vector<bool> farOnly{false, true};
    const std::optional<Hit> hit = scene.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0}, 0.0, &farOnly);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->mesh, 1U);
}

TEST(SceneRays, OneRayScenePerSetOfMeshes)
{
    auto square = std::make_shared<Mesh>();
    addSquare(*square, 2.0, 0);
    const auto body = std::make_shared<const snapshot::Body>(snapshot::Body{.mesh = square});
    snapshot::Scene scene{.bodies = {body}};
    SceneRays rays;
    EXPECT_EQ(rays.rays(snapshot::Scene{}).scene, nullptr);
    const std::shared_ptr<const RayScene> first = rays.rays(scene).scene;
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(rays.rays(scene).scene, first);

    snapshot::Body recolored = *body;
    recolored.appearance.color = snapshot::Color{1.0F, 0.0F, 0.0F};
    scene.bodies = {std::make_shared<const snapshot::Body>(recolored)};
    EXPECT_EQ(rays.rays(scene).scene, first) << "a color edit keeps the mesh and the ray scene";
    EXPECT_EQ(rays.builds(), 1U);

    auto moved = std::make_shared<Mesh>();
    addSquare(*moved, 4.0, 0);
    scene.bodies = {std::make_shared<const snapshot::Body>(snapshot::Body{.mesh = moved})};
    const VisibleRays second = rays.rays(scene);
    EXPECT_NE(second.scene, first);
    EXPECT_EQ(rays.builds(), 2U);
    const std::optional<Hit> hit = second.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0});
    ASSERT_TRUE(hit);
    EXPECT_NEAR(hit->distance, 4.0, kTolerance);
}

TEST(SceneRays, HidingABodyKeepsTheRaySceneAndItsRaysPassIt)
{
    auto near = std::make_shared<Mesh>();
    addSquare(*near, 2.0, 0);
    auto far = std::make_shared<Mesh>();
    addSquare(*far, 4.0, 0);
    const auto front = std::make_shared<const snapshot::Body>(snapshot::Body{.mesh = near});
    const auto back = std::make_shared<const snapshot::Body>(snapshot::Body{.feature = 1, .mesh = far});
    SceneRays rays;
    const VisibleRays shown = rays.rays(snapshot::Scene{.bodies = {front, back}});
    const std::optional<Hit> nearHit = shown.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0});
    ASSERT_TRUE(nearHit);
    EXPECT_NEAR(nearHit->distance, 2.0, kTolerance);

    const VisibleRays hidden = rays.rays(snapshot::Scene{.bodies = {back}, .hidden = {front}});
    EXPECT_EQ(hidden.scene, shown.scene);
    EXPECT_EQ(hidden.meshes, shown.meshes);
    EXPECT_EQ(rays.builds(), 1U);
    const std::optional<Hit> farHit = hidden.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0});
    ASSERT_TRUE(farHit);
    EXPECT_NEAR(farHit->distance, 4.0, kTolerance);

    EXPECT_FALSE(rays.rays(snapshot::Scene{.hidden = {front, back}}).first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0}));
    EXPECT_EQ(rays.builds(), 1U);
}

TEST(SceneRays, APreparedSceneIsBuiltOnce)
{
    auto square = std::make_shared<Mesh>();
    addSquare(*square, 2.0, 0);
    const snapshot::Scene scene{.bodies = {std::make_shared<const snapshot::Body>(snapshot::Body{.mesh = square})}};
    SceneRays rays;
    rays.prepare(snapshot::Scene{});
    EXPECT_EQ(rays.builds(), 0U);
    rays.prepare(scene);
    rays.prepare(scene);
    const VisibleRays prepared = rays.rays(scene);
    ASSERT_NE(prepared.scene, nullptr);
    EXPECT_TRUE(prepared.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0}));
    rays.prepare(scene);
    EXPECT_EQ(rays.rays(scene).scene, prepared.scene);
    EXPECT_EQ(rays.builds(), 1U);
}

TEST(SceneRays, AStalePreparedSceneIsNotUsed)
{
    auto near = std::make_shared<Mesh>();
    addSquare(*near, 2.0, 0);
    auto far = std::make_shared<Mesh>();
    addSquare(*far, 4.0, 0);
    SceneRays rays;
    rays.prepare(snapshot::Scene{.bodies = {std::make_shared<const snapshot::Body>(snapshot::Body{.mesh = near})}});
    const VisibleRays current = rays.rays(Meshes{far}, Meshes{far});
    ASSERT_NE(current.scene, nullptr);
    const std::optional<Hit> hit = current.first({0.2, 0.3, 0.0}, {0.0, 0.0, 1.0});
    ASSERT_TRUE(hit);
    EXPECT_NEAR(hit->distance, 4.0, kTolerance);
}

} // namespace
} // namespace kalve137::pick
