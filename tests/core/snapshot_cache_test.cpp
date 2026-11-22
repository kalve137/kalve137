// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"
#include "core/recompute.h"
#include "core/snapshot_cache.h"

#include "geometry/shapes.h"

#include <gtest/gtest.h>

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cstddef>

namespace kalve137::core {
namespace {

struct Pair {
    Document document;
    FeatureId box = 0;
    FeatureId pin = 0;
};

Pair importPair()
{
    Pair pair;
    pair.box = pair.document.add(ImportFeature{"box.step", {{"Box", geometry::makeBox(10.0, 10.0, 10.0)}}});
    pair.pin = pair.document.add(
        ImportFeature{"pin.step", {{"Pin", geometry::makeCylinder(gp_Pnt(30.0, 0.0, 0.0), 2.0, 10.0)}}});
    return pair;
}

void movePin(Pair& pair)
{
    MoveFeature move;
    move.bodies = {{pair.pin, 0}};
    move.translation = gp_Vec(0.0, 20.0, 0.0);
    pair.document.add(move);
}

bool sameMesh(const snapshot::Mesh& a, const snapshot::Mesh& b)
{
    return a.vertices == b.vertices && a.triangles == b.triangles && a.faces == b.faces && a.faceCount == b.faceCount &&
           a.edges == b.edges && a.edgeCount == b.edgeCount && a.points == b.points;
}

TEST(SnapshotCache, BuildsOneBodyPerModelBody)
{
    SnapshotCache cache;
    const snapshot::Scene scene = cache.scene(recompute(importPair().document));
    ASSERT_EQ(scene.bodies.size(), 2U);
    EXPECT_EQ(cache.size(), 2U);
    EXPECT_EQ(scene.bodies[0]->mesh.faceCount, 6U);
    EXPECT_EQ(scene.bodies[1]->mesh.faceCount, 3U);
}

TEST(SnapshotCache, AnEditToOneBodyLeavesTheOthersUntouched)
{
    Pair pair = importPair();
    SnapshotCache cache;
    const snapshot::Scene before = cache.scene(recompute(pair.document));
    movePin(pair);
    const snapshot::Scene after = cache.scene(recompute(pair.document));
    ASSERT_EQ(after.bodies.size(), 2U);
    EXPECT_EQ(after.bodies[0], before.bodies[0]);
    EXPECT_NE(after.bodies[1], before.bodies[1]);
}

TEST(SnapshotCache, AnUndoneEditGivesTheEarlierSnapshot)
{
    Pair pair = importPair();
    const Document earlier = pair.document;
    SnapshotCache cache;
    const snapshot::Scene before = cache.scene(recompute(pair.document));
    movePin(pair);
    cache.scene(recompute(pair.document));
    cache.clear();
    const snapshot::Scene undone = cache.scene(recompute(earlier));
    ASSERT_EQ(undone.bodies.size(), before.bodies.size());
    for (std::size_t i = 0; i < undone.bodies.size(); ++i) {
        EXPECT_TRUE(sameMesh(undone.bodies[i]->mesh, before.bodies[i]->mesh));
    }
}

TEST(SnapshotCache, TheOriginSitsOnAMeterGridNearTheBodies)
{
    Pair pair;
    pair.pin = pair.document.add(
        ImportFeature{"far.step", {{"Far", geometry::makeCylinder(gp_Pnt(1'000'250.0, -1'400.0, 0.0), 50.0, 100.0)}}});
    SnapshotCache cache;
    const snapshot::Scene before = cache.scene(recompute(pair.document));
    EXPECT_EQ(before.origin, (snapshot::Point{1'000'000.0, -1'000.0, 0.0}));
    movePin(pair);
    EXPECT_EQ(cache.scene(recompute(pair.document)).origin, before.origin);
    EXPECT_EQ(cache.scene(Model{}).origin, (snapshot::Point{}));
}

TEST(SnapshotCache, DropsBodiesThatLeftTheModel)
{
    Pair pair = importPair();
    SnapshotCache cache;
    cache.scene(recompute(pair.document));
    pair.document.setMarker(1);
    cache.scene(recompute(pair.document));
    EXPECT_EQ(cache.size(), 1U);
}

} // namespace
} // namespace kalve137::core
