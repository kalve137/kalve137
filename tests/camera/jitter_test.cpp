// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/views.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>

using namespace kalve137::camera;

namespace {

using Mat4f = std::array<float, 16>;

constexpr Viewport kViewport{1920, 1080, 1.0};
constexpr Vec3 kBodyOrigin{1.0e6, 0.0, 0.0};
constexpr double kBodySize = 50.0;
constexpr double kStepDegrees = 0.01;
constexpr int kSteps = 100;

Mat4f multiply(const Mat4f& a, const Mat4f& b)
{
    Mat4f product{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float sum = 0.0F;
            for (int k = 0; k < 4; ++k) {
                sum += a[Mat4::index(row, k)] * b[Mat4::index(k, column)];
            }
            product[Mat4::index(row, column)] = sum;
        }
    }
    return product;
}

ScreenPoint toScreen(const Mat4f& m, float x, float y, float z)
{
    const auto row = [&](int r) {
        const auto at = [&](int c) { return m[Mat4::index(r, c)]; };
        return at(0) * x + at(1) * y + at(2) * z + at(3);
    };
    const float w = row(3);
    const double ndcX = row(0) / w;
    const double ndcY = row(1) / w;
    return {(ndcX + 1.0) * 0.5 * kViewport.width, kViewport.height - 1 - (ndcY + 1.0) * 0.5 * kViewport.height};
}

struct Errors {
    double relative = 0.0;
    double world = 0.0;
};

Errors orbitFarBody(Projection projection)
{
    const Box body{kBodyOrigin, kBodyOrigin + Vec3{kBodySize, kBodySize, kBodySize}};
    Camera camera;
    camera.setProjectionType(projection);
    camera.setAspect(kViewport.aspect());
    camera.setEyeAndCenter(body.center() + Vec3{100.0, -100.0, 100.0}, body.center());
    camera.setUp({0.0, 0.0, 1.0});
    homeView(camera);
    EXPECT_TRUE(fit(camera, body));
    camera.fitDepth(body);

    Errors errors;
    const double step = kStepDegrees * std::numbers::pi / 180.0;
    for (int i = 0; i < kSteps; ++i) {
        camera.transform(Motion::rotationAbout(body.center(), {0.0, 0.0, 1.0}, step));
        const Mat4f projectionF = camera.projection().toFloat();
        const Mat4f relative = multiply(projectionF, camera.viewRelativeToF(kBodyOrigin));
        const Mat4f world = multiply(projectionF, camera.view().toFloat());
        for (const Vec3& corner : body.corners()) {
            const ScreenPoint exact = camera.project(corner, kViewport);
            const Vec3 local = corner - kBodyOrigin;
            const ScreenPoint near = toScreen(relative, static_cast<float>(local.x), static_cast<float>(local.y),
                                              static_cast<float>(local.z));
            const ScreenPoint far = toScreen(world, static_cast<float>(corner.x), static_cast<float>(corner.y),
                                             static_cast<float>(corner.z));
            errors.relative = std::max({errors.relative, std::abs(near.x - exact.x), std::abs(near.y - exact.y)});
            errors.world = std::max({errors.world, std::abs(far.x - exact.x), std::abs(far.y - exact.y)});
        }
    }
    return errors;
}

} // namespace

TEST(Jitter, BodyOneKilometerOutStaysStillWhileOrbiting)
{
    for (const Projection projection : {Projection::Orthographic, Projection::Perspective}) {
        const Errors errors = orbitFarBody(projection);
        EXPECT_LT(errors.relative, 0.01);
        EXPECT_GT(errors.world, 20.0 * errors.relative);
        RecordProperty(projection == Projection::Orthographic ? "orthographic" : "perspective",
                       std::to_string(errors.relative) + " px against " + std::to_string(errors.world) + " px");
    }
}
