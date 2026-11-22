// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "camera/navigation.h"

#include <optional>

namespace kalve137::camera {

inline constexpr double kFitMargin = 0.3;

void orient(Camera& camera, const Vec3& back, const Vec3& up);

void homeView(Camera& camera);
void standardView(Camera& camera, StandardView view);
void lookAt(Camera& camera, const Vec3& normal, const Vec3& yDirection);

bool facesStandardView(const Camera& camera);
void turn(Camera& camera, ViewTurn way);

bool fit(Camera& camera, const std::optional<Box>& bounds);
bool fitAround(Camera& camera, double halfSize);

} // namespace kalve137::camera
