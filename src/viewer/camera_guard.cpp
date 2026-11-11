// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/camera_guard.h"

#include <Graphic3d_Camera.hxx>
#include <gp.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>

namespace kalve137::viewer {

namespace {

constexpr double kMaxUpAlignment = 0.999;

bool isFinite(const gp_XYZ& v)
{
    return std::isfinite(v.X()) && std::isfinite(v.Y()) && std::isfinite(v.Z());
}

bool isPositive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

} // namespace

bool isUsable(const Graphic3d_Camera& camera)
{
    return isFinite(camera.Eye().XYZ()) && isFinite(camera.Center().XYZ()) && isFinite(camera.Up().XYZ()) &&
           isPositive(camera.Scale()) && isPositive(camera.Aspect());
}

bool repairUp(Graphic3d_Camera& camera)
{
    if (!isUsable(camera)) {
        return false;
    }
    const gp_Dir direction = camera.Direction();
    if (std::abs(direction.Dot(camera.Up())) < kMaxUpAlignment) {
        return false;
    }
    const gp_Dir hint = std::abs(direction.Z()) < kMaxUpAlignment ? gp::DZ() : gp::DY();
    const gp_Vec up = gp_Vec(hint) - gp_Vec(direction) * hint.Dot(direction);
    camera.SetUp(gp_Dir(up));
    return true;
}

void resetCamera(Graphic3d_Camera& camera, double halfSize)
{
    camera.SetEyeAndCenter(gp_Pnt(0.0, 0.0, 2.0 * halfSize), gp::Origin());
    camera.SetUp(gp::DY());
    camera.SetScale(2.0 * halfSize);
    if (!isPositive(camera.Aspect())) {
        camera.SetAspect(1.0);
    }
}

} // namespace kalve137::viewer
