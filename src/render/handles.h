// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "render/draw_list.h"
#include "snapshot/color.h"
#include "snapshot/mesh.h"

#include <span>
#include <vector>

namespace kalve137::render {

struct HandleColors {
    snapshot::Color fill;
    snapshot::Color outline;
    snapshot::Color halo;

    bool operator==(const HandleColors&) const = default;
};

struct HandleItem {
    camera::Vec3 anchor;
    camera::Vec3 toward;
    bool active = true;
    bool hovered = false;
    bool dragged = false;
    HandleColors colors;
    std::vector<camera::Vec3> guide;

    bool operator==(const HandleItem&) const = default;
};

struct HandleGeometry {
    std::vector<Handle> arrows;
    std::vector<DatumLine> guides;
    std::vector<DatumLine> outlines;

    bool operator==(const HandleGeometry&) const = default;
};

snapshot::Color lighter(const snapshot::Color& color, int percent);
Handle placeHandle(const HandleItem& item, const snapshot::Point& origin);
HandleGeometry placeHandles(std::span<const HandleItem> items, const snapshot::Point& origin);

} // namespace kalve137::render
