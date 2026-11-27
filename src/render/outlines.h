// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/draw_list.h"
#include "render/style.h"
#include "snapshot/color.h"
#include "snapshot/mesh.h"

#include <span>
#include <vector>

namespace kalve137::render {

struct OutlineItem {
    snapshot::Polyline points;
    snapshot::Color color;
    double width = kHighlightEdgeWidth;

    bool operator==(const OutlineItem&) const = default;
};

std::vector<DatumLine> placeOutlines(std::span<const OutlineItem> items, const snapshot::Point& origin);

} // namespace kalve137::render
