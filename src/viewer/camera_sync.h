// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"

#include <Graphic3d_Camera.hxx>

namespace kalve137::viewer {

void applyCamera(const camera::Camera& from, Graphic3d_Camera& to);
camera::Camera readCamera(const Handle(Graphic3d_Camera)& from);

} // namespace kalve137::viewer
