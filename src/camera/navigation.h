// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

namespace kalve137::camera {

enum class NavigationPreset {
    MiddleOrbitsCtrlPans,
    MiddlePansShiftOrbits,
    MiddleOrbitsShiftPans,
    RightOrbitsCtrlPans,
};

struct Navigation {
    NavigationPreset preset = NavigationPreset::MiddleOrbitsCtrlPans;
    bool reverseZoom = false;
    bool zoomToCursor = true;
    bool orbitAroundCursor = false;
    bool constrainedOrbit = false;
    bool animateTransitions = true;
};

enum class StandardView { Front, Back, Left, Right, Top, Bottom };

enum class ViewTurn { Up, Down, Left, Right, RollClockwise, RollCounterclockwise };

enum class Gesture { Orbit, Pan, Zoom };

enum Button : unsigned { NoButton = 0, LeftButton = 1, RightButton = 2, MiddleButton = 4 };

enum Modifier : unsigned { NoModifier = 0, Shift = 1, Ctrl = 2, Alt = 4 };

std::optional<Gesture> gestureFor(NavigationPreset preset, unsigned buttons, unsigned modifiers);

} // namespace kalve137::camera
