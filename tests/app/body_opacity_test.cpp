// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/body_opacity.h"

#include "snapshot/body.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace kalve137::app {
namespace {

std::shared_ptr<const snapshot::Body> bodyWith(int part)
{
    snapshot::Body body;
    body.feature = 1;
    body.part = part;
    body.mesh = std::make_shared<const snapshot::Mesh>();
    return std::make_shared<const snapshot::Body>(std::move(body));
}

TEST(BodyOpacity, PlanesModeFadesEveryBodyAndKeepsItsMesh)
{
    const auto shown = bodyWith(0);
    const auto hidden = bodyWith(1);
    const snapshot::Scene scene =
        withBodyOpacity({.bodies = {shown}, .origin = {}, .hidden = {hidden}}, PickMode::Planes);
    ASSERT_EQ(scene.bodies.size(), 1U);
    ASSERT_EQ(scene.hidden.size(), 1U);
    EXPECT_LT(scene.bodies[0]->opacity, 1.0F);
    EXPECT_GT(scene.bodies[0]->opacity, 0.0F);
    EXPECT_FLOAT_EQ(scene.hidden[0]->opacity, scene.bodies[0]->opacity);
    EXPECT_EQ(scene.bodies[0]->mesh, shown->mesh);
    EXPECT_EQ(scene.bodies[0]->part, shown->part);
}

TEST(BodyOpacity, OtherModesShowBodiesOpaqueAndKeepUnchangedBodies)
{
    const auto shown = bodyWith(0);
    const snapshot::Scene same = withBodyOpacity({.bodies = {shown}, .origin = {}, .hidden = {}}, PickMode::Faces);
    EXPECT_EQ(same.bodies[0], shown) << "body that already has the opacity is not copied";

    const snapshot::Scene faded = withBodyOpacity(same, PickMode::Planes);
    const snapshot::Scene again = withBodyOpacity(faded, PickMode::Planes);
    EXPECT_EQ(again.bodies[0], faded.bodies[0]);
    const snapshot::Scene back = withBodyOpacity(faded, PickMode::Faces);
    EXPECT_FLOAT_EQ(back.bodies[0]->opacity, 1.0F);
}

TEST(BodyOpacity, APreviewGhostStaysHalfTransparentInEveryMode)
{
    snapshot::Body preview = *bodyWith(0);
    preview.ghost = true;
    const auto ghost = std::make_shared<const snapshot::Body>(std::move(preview));
    const auto model = bodyWith(1);
    for (const PickMode mode : {PickMode::None, PickMode::Faces, PickMode::Edges, PickMode::Planes}) {
        const snapshot::Scene scene = withBodyOpacity({.bodies = {ghost, model}, .origin = {}, .hidden = {}}, mode);
        EXPECT_FLOAT_EQ(scene.bodies[0]->opacity, bodyOpacity(PickMode::Planes));
        EXPECT_TRUE(scene.bodies[0]->ghost);
        EXPECT_FLOAT_EQ(scene.bodies[1]->opacity, bodyOpacity(mode));
    }
}

} // namespace
} // namespace kalve137::app
