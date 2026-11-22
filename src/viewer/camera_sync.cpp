// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/camera_sync.h"

#include <Graphic3d_Camera.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

namespace kalve137::viewer {

namespace {

gp_Pnt toPoint(const camera::Vec3& v)
{
    return {v.x, v.y, v.z};
}

gp_Dir toDir(const camera::Vec3& v)
{
    return {v.x, v.y, v.z};
}

camera::Vec3 toVec(const gp_XYZ& v)
{
    return {v.X(), v.Y(), v.Z()};
}

} // namespace

void applyCamera(const camera::Camera& from, Graphic3d_Camera& to)
{
    if (!from.isUsable()) {
        return;
    }
    to.SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
    to.SetScale(from.orthographicScale());
    to.SetProjectionType(from.isOrthographic() ? Graphic3d_Camera::Projection_Orthographic
                                               : Graphic3d_Camera::Projection_Perspective);
    to.SetFOVy(from.fovY());
    to.SetZRange(from.zNear(), from.zFar());
    to.SetAspect(from.aspect());
    to.SetUp(toDir(from.up()));
    to.SetEyeAndCenter(toPoint(from.eye()), toPoint(from.center()));
    to.SetDirectionFromEye(toDir(from.direction()));
}

camera::Camera readCamera(const Handle(Graphic3d_Camera)& from)
{
    camera::Camera to;
    to.setScale(from->Scale());
    to.setProjectionType(from->IsOrthographic() ? camera::Projection::Orthographic : camera::Projection::Perspective);
    to.setFovY(from->FOVy());
    to.setZRange(from->ZNear(), from->ZFar());
    to.setAspect(from->Aspect());
    to.setUp(toVec(from->Up().XYZ()));
    to.setEyeAndCenter(toVec(from->Eye().XYZ()), toVec(from->Center().XYZ()));
    to.setDirectionFromEye(toVec(from->Direction().XYZ()));
    return to;
}

} // namespace kalve137::viewer
