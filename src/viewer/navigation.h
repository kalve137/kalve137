// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace kalve137::viewer {

enum class NavigationPreset {
    MiddleOrbitsCtrlPans,
    MiddlePansShiftOrbits,
    MiddleOrbitsShiftPans,
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

} // namespace kalve137::viewer
