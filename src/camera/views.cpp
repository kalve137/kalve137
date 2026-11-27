// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-FileCopyrightText: 1999-2014 OPEN CASCADE SAS
// SPDX-License-Identifier: GPL-3.0-or-later AND LGPL-2.1-only WITH OCCT-exception-1.0

#include "camera/views.h"

#include <algorithm>
#include <cmath>

namespace kalve137::camera {

namespace {

constexpr double kMinAxisAlignment = 0.9999;
constexpr Vec3 kX{1.0, 0.0, 0.0};
constexpr Vec3 kY{0.0, 1.0, 0.0};
constexpr Vec3 kZ{0.0, 0.0, 1.0};

bool isAxisAligned(const Vec3& direction)
{
    return std::max({std::abs(direction.x), std::abs(direction.y), std::abs(direction.z)}) >= kMinAxisAlignment;
}

Vec3 nearestAxis(const Vec3& direction)
{
    const double x = std::abs(direction.x);
    const double y = std::abs(direction.y);
    const double z = std::abs(direction.z);
    if (x >= y && x >= z) {
        return {std::copysign(1.0, direction.x), 0.0, 0.0};
    }
    if (y >= z) {
        return {0.0, std::copysign(1.0, direction.y), 0.0};
    }
    return {0.0, 0.0, std::copysign(1.0, direction.z)};
}

} // namespace

void orient(Camera& camera, const Vec3& back, const Vec3& up)
{
    const Vec3 origin = camera.worldToView({});
    const double reach = length(camera.eye());
    camera.setEyeAndCenter(back * reach, {});
    camera.setDirectionFromEye(-back);
    camera.setUp(up);
    camera.orthogonalizeUp();
    const Vec3 size = camera.viewDimensions();
    camera.translateInView(-origin.x, -origin.y);
    camera.scaleTo(size.x, size.y);
}

void homeView(Camera& camera)
{
    orient(camera, normalized(Vec3{1.0, -1.0, 1.0}), kZ);
}

void standardView(Camera& camera, StandardView view)
{
    switch (view) {
    case StandardView::Front:
        orient(camera, -kY, kZ);
        return;
    case StandardView::Back:
        orient(camera, kY, kZ);
        return;
    case StandardView::Left:
        orient(camera, -kX, kZ);
        return;
    case StandardView::Right:
        orient(camera, kX, kZ);
        return;
    case StandardView::Top:
        orient(camera, kZ, kY);
        return;
    case StandardView::Bottom:
        orient(camera, -kZ, -kY);
        return;
    }
}

void lookAt(Camera& camera, const Vec3& normal, const Vec3& yDirection)
{
    camera.setDirection(-normal);
    camera.setUp(yDirection);
}

bool facesStandardView(const Camera& camera)
{
    return isAxisAligned(camera.direction()) && isAxisAligned(camera.up());
}

void turn(Camera& camera, ViewTurn way)
{
    const Vec3 direction = nearestAxis(camera.direction());
    const Vec3 up = nearestAxis(camera.up());
    const Vec3 right = cross(direction, up);
    Vec3 newDirection = direction;
    Vec3 newUp = up;
    switch (way) {
    case ViewTurn::Up:
        newDirection = -up;
        newUp = direction;
        break;
    case ViewTurn::Down:
        newDirection = up;
        newUp = -direction;
        break;
    case ViewTurn::Left:
        newDirection = right;
        break;
    case ViewTurn::Right:
        newDirection = -right;
        break;
    case ViewTurn::RollClockwise:
        newUp = -right;
        break;
    case ViewTurn::RollCounterclockwise:
        newUp = right;
        break;
    }
    const Vec3 center = camera.center();
    camera.setEyeAndCenter(center - newDirection * camera.distance(), center);
    camera.setUp(newUp);
}

bool fit(Camera& camera, const std::optional<Box>& bounds)
{
    if (!bounds) {
        return false;
    }
    Camera fitted = camera;
    if (!fitted.fit(*bounds, kFitMargin) || !fitted.isUsable()) {
        return false;
    }
    camera = fitted;
    return true;
}

bool fitAround(Camera& camera, double halfSize)
{
    if (!std::isfinite(halfSize) || halfSize <= 0.0) {
        return false;
    }
    return fit(camera, Box{{-halfSize, -halfSize, -halfSize}, {halfSize, halfSize, halfSize}});
}

} // namespace kalve137::camera
