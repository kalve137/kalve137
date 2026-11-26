// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../camera/occt_compare.h"

#include "app/feature/distance_handle.h"
#include "app/view/view_projection.h"
#include "camera/camera.h"
#include "viewer/occt_view_widget.h"

#include <gtest/gtest.h>

#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <QPointF>

#include <array>
#include <cmath>
#include <numbers>
#include <optional>

using namespace kalve137;
using camera::Camera;
using camera::Projection;
using camera::Vec3;

namespace {

constexpr camera::Viewport kViewport{1600, 1000, 1.25};
constexpr double kAgreePixels = 0.5;
constexpr int kPointStride = 40;
constexpr int kHitReach = 60;
constexpr std::array kProjections{Projection::Perspective, Projection::Orthographic};

Camera placed(Projection projection)
{
    Camera camera;
    camera.setProjectionType(projection);
    camera.setEyeAndCenter({120.0, -340.0, 260.0}, {10.0, 20.0, -5.0});
    camera.setUp({0.0, 0.0, 1.0});
    camera.setScale(300.0);
    camera.setAspect(kViewport.aspect());
    camera.setZRange(5.0, 2000.0);
    return camera;
}

QPointF through(app::ViewProjection& projection, bool camera, const gp_Pnt& point)
{
    projection.setCameraInput(camera);
    return projection.toScreen(point);
}

bool hitThrough(app::ViewProjection& projection, const app::DistanceHandle& handle, bool camera, QPointF pos)
{
    projection.setCameraInput(camera);
    return handle.hits(pos);
}

bool agreesNearby(app::ViewProjection& projection, const app::DistanceHandle& handle, QPointF pos, bool expected)
{
    for (const QPointF offset : {QPointF(kAgreePixels, 0.0), QPointF(-kAgreePixels, 0.0), QPointF(0.0, kAgreePixels),
                                 QPointF(0.0, -kAgreePixels)}) {
        if (hitThrough(projection, handle, true, pos + offset) == expected) {
            return true;
        }
    }
    return false;
}

struct HitCounts {
    int hits = 0;
    int unmatched = 0;
};

HitCounts compareHits(app::ViewProjection& projection, const app::DistanceHandle& handle, const gp_Pnt& anchor)
{
    HitCounts counts;
    const QPointF center = through(projection, false, anchor);
    for (int dy = -kHitReach; dy <= kHitReach; ++dy) {
        for (int dx = -kHitReach; dx <= kHitReach; ++dx) {
            const QPointF pos(std::round(center.x()) + dx, std::round(center.y()) + dy);
            const bool view = hitThrough(projection, handle, false, pos);
            const bool camera = hitThrough(projection, handle, true, pos);
            counts.hits += view ? 1 : 0;
            if (view != camera && !agreesNearby(projection, handle, pos, view)) {
                ++counts.unmatched;
            }
        }
    }
    return counts;
}

} // namespace

TEST(ViewProjectionTest, ProjectsAPointLikeTheViewOnAGridOfPixels)
{
    viewer::OcctViewWidget hidden;
    app::ViewProjection projection(hidden, nullptr);
    ASSERT_FALSE(projection.hasCameraInput());
    EXPECT_EQ(projection.canvas(), &hidden);
    const Vec3 origin{0.0, 0.0, 12.0};
    const Vec3 normal{0.0, 0.0, 1.0};
    for (const Projection kind : kProjections) {
        const Camera camera = placed(kind);
        hidden.setSyncedCamera(camera, kViewport);
        ASSERT_TRUE(projection.projection());
        int compared = 0;
        for (int y = 0; y <= kViewport.height; y += kPointStride) {
            for (int x = 0; x <= kViewport.width; x += kPointStride) {
                const camera::ScreenPoint pixel{static_cast<double>(x), static_cast<double>(y)};
                const std::optional<Vec3> world = camera::onPlane(camera.unproject(pixel, kViewport), origin, normal);
                if (!world) {
                    continue;
                }
                const gp_Pnt point = test::toPnt(*world);
                const QPointF view = through(projection, false, point);
                const QPointF shown = through(projection, true, point);
                EXPECT_LE(std::hypot(view.x() - shown.x(), view.y() - shown.y()), kAgreePixels)
                    << "pixel " << x << "," << y;
                EXPECT_LE(std::hypot(shown.x() - pixel.x, shown.y() - pixel.y), kAgreePixels)
                    << "pixel " << x << "," << y;
                ++compared;
            }
        }
        EXPECT_GT(compared, 0);
        projection.setCameraInput(false);
        const double view = projection.worldPerPixel();
        projection.setCameraInput(true);
        EXPECT_NEAR(projection.worldPerPixel(), view, view * 1e-6);
    }
}

TEST(ViewProjectionTest, HitsAHandleLikeTheViewOnAGridOfPixels)
{
    viewer::OcctViewWidget hidden;
    app::ViewProjection projection(hidden, nullptr);
    app::DistanceHandle handle(projection);
    const gp_Pnt origin(10.0, 20.0, -5.0);
    for (const Projection kind : kProjections) {
        hidden.setSyncedCamera(placed(kind), kViewport);

        handle.show(origin, gp_Dir(0.0, 0.0, 1.0), 30.0, 1.0);
        const HitCounts straight = compareHits(projection, handle, origin.Translated(gp_Vec(0.0, 0.0, 30.0)));
        EXPECT_GT(straight.hits, 0);
        EXPECT_EQ(straight.unmatched, 0);

        const gp_Ax1 axis(origin, gp_Dir(0.0, 0.0, 1.0));
        const gp_Pnt start = origin.Translated(gp_Vec(40.0, 0.0, 0.0));
        handle.showArc(axis, start, 60.0);
        const HitCounts arc = compareHits(projection, handle, start.Rotated(axis, std::numbers::pi / 3.0));
        EXPECT_GT(arc.hits, 0);
        EXPECT_EQ(arc.unmatched, 0);
        handle.hide();
    }
}
