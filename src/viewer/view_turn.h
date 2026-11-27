// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/navigation.h"

class Graphic3d_Camera;

namespace kalve137::viewer {

using camera::ViewTurn;

bool facesStandardView(const Graphic3d_Camera& camera);

void turn(Graphic3d_Camera& camera, ViewTurn turn);

} // namespace kalve137::viewer
