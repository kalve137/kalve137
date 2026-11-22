// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"
#include "core/recompute.h"
#include "core/snapshot_cache.h"

#include "geometry/shapes.h"

#include <gtest/gtest.h>

#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
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
    EXPECT_EQ(scene.bodies[0]->mesh->faceCount, 6U);
    EXPECT_EQ(scene.bodies[1]->mesh->faceCount, 3U);
    EXPECT_EQ(scene.bodies[0]->mesh->low, (snapshot::Point{0.0, 0.0, 0.0}));
    EXPECT_EQ(scene.bodies[0]->mesh->high, (snapshot::Point{10.0, 10.0, 10.0}));
}

TEST(SnapshotCache, AColorEditKeepsTheMesh)
{
    Model model = recompute(importPair().document);
    SnapshotCache cache;
    const snapshot::Scene before = cache.scene(model);
    model.bodies[0].color = geometry::Color{1.0, 0.0, 0.0};
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(model.bodies[0].shape, TopAbs_FACE, faces);
    model.bodies[0].faceColors.set(TopoDS::Face(faces(3)), geometry::Color{0.0, 0.0, 1.0});
    const snapshot::Scene after = cache.scene(model);
    ASSERT_EQ(after.bodies.size(), 2U);
    EXPECT_NE(after.bodies[0], before.bodies[0]);
    EXPECT_EQ(after.bodies[0]->mesh, before.bodies[0]->mesh);
    EXPECT_EQ(after.bodies[1], before.bodies[1]);
    const snapshot::Appearance& appearance = after.bodies[0]->appearance;
    EXPECT_EQ(appearance.color, (snapshot::Color{1.0F, 0.0F, 0.0F}));
    ASSERT_EQ(appearance.faces.size(), 6U);
    EXPECT_EQ(appearance.faces[2], (snapshot::Color{0.0F, 0.0F, 1.0F}));
    EXPECT_FALSE(appearance.faces[0].has_value());
    EXPECT_EQ(cache.scene(model).bodies[0], after.bodies[0]);
}

TEST(SnapshotCache, HiddenBodiesLeaveTheSceneButKeepTheOrigin)
{
    Pair pair;
    pair.box = pair.document.add(ImportFeature{"box.step", {{"Box", geometry::makeBox(10.0, 10.0, 10.0)}}});
    pair.pin = pair.document.add(
        ImportFeature{"far.step", {{"Far", geometry::makeCylinder(gp_Pnt(5'000.0, 0.0, 0.0), 2.0, 10.0)}}});
    const Model model = recompute(pair.document);
    SnapshotCache cache;
    const snapshot::Scene all = cache.scene(model);
    const snapshot::Scene shown = cache.scene(model, [&](BodyKey key) { return key.feature != pair.pin; });
    ASSERT_EQ(shown.bodies.size(), 1U);
    EXPECT_EQ(shown.bodies[0], all.bodies[0]);
    EXPECT_EQ(shown.origin, all.origin);
    EXPECT_EQ(cache.size(), 2U);
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
        EXPECT_TRUE(sameMesh(*undone.bodies[i]->mesh, *before.bodies[i]->mesh));
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
