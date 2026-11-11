// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class Graphic3d_Camera;

namespace kalve137::viewer {

bool isUsable(const Graphic3d_Camera& camera);

bool repairUp(Graphic3d_Camera& camera);

void resetCamera(Graphic3d_Camera& camera, double halfSize);

} // namespace kalve137::viewer
