// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "render/draw_list.h"
#include "snapshot/color.h"
#include "snapshot/mesh.h"

#include <optional>
#include <vector>

namespace kalve137::render {

struct MeasureLine {
    camera::Vec3 from;
    camera::Vec3 to;
    snapshot::Color color;

    bool operator==(const MeasureLine&) const = default;
};

struct MeasureItems {
    std::optional<MeasureLine> line;
    std::vector<MarkerItem> markers;

    bool operator==(const MeasureItems&) const = default;
};

std::vector<DatumLine> placeMeasureLines(const MeasureItems& items, const snapshot::Point& origin);

} // namespace kalve137::render
