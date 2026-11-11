// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class Graphic3d_Camera;

namespace kalve137::viewer {

enum class ViewTurn { Up, Down, Left, Right, RollClockwise, RollCounterclockwise };

bool facesStandardView(const Graphic3d_Camera& camera);

void turn(Graphic3d_Camera& camera, ViewTurn turn);

} // namespace kalve137::viewer
