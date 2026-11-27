// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "camera/math.h"

#include <span>

namespace kalve137::camera {

Vec3 gravityPoint(const Camera& camera, std::span<const Box> boxes);

} // namespace kalve137::camera
