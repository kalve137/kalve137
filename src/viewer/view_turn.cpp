// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/view_turn.h"

#include <Graphic3d_Camera.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>

namespace kalve137::viewer {

namespace {

constexpr double kMinAxisAlignment = 0.9999;

bool isAxisAligned(const gp_Dir& direction)
{
    return std::max({std::abs(direction.X()), std::abs(direction.Y()), std::abs(direction.Z())}) >= kMinAxisAlignment;
}

gp_Dir nearestAxis(const gp_Dir& direction)
{
    const double x = std::abs(direction.X());
    const double y = std::abs(direction.Y());
    const double z = std::abs(direction.Z());
    if (x >= y && x >= z) {
        return {std::copysign(1.0, direction.X()), 0.0, 0.0};
    }
    if (y >= z) {
        return {0.0, std::copysign(1.0, direction.Y()), 0.0};
    }
    return {0.0, 0.0, std::copysign(1.0, direction.Z())};
}

} // namespace

bool facesStandardView(const Graphic3d_Camera& camera)
{
    return isAxisAligned(camera.Direction()) && isAxisAligned(camera.Up());
}

void turn(Graphic3d_Camera& camera, ViewTurn turn)
{
    const gp_Dir direction = nearestAxis(camera.Direction());
    const gp_Dir up = nearestAxis(camera.Up());
    const gp_Dir right = direction.Crossed(up);
    gp_Dir newDirection = direction;
    gp_Dir newUp = up;
    switch (turn) {
    case ViewTurn::Up:
        newDirection = up.Reversed();
        newUp = direction;
        break;
    case ViewTurn::Down:
        newDirection = up;
        newUp = direction.Reversed();
        break;
    case ViewTurn::Left:
        newDirection = right;
        break;
    case ViewTurn::Right:
        newDirection = right.Reversed();
        break;
    case ViewTurn::RollClockwise:
        newUp = right.Reversed();
        break;
    case ViewTurn::RollCounterclockwise:
        newUp = right;
        break;
    }
    const gp_Pnt center = camera.Center();
    const double distance = camera.Distance();
    camera.SetEyeAndCenter(center.Translated(gp_Vec(newDirection) * -distance), center);
    camera.SetUp(newUp);
}

} // namespace kalve137::viewer
