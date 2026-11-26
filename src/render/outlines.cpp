// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/outlines.h"

#include "render/positions.h"
#include "render/style.h"

#include <cstddef>

namespace kalve137::render {

std::vector<DatumLine> placeOutlines(std::span<const OutlineItem> items, const snapshot::Point& origin)
{
    std::vector<DatumLine> lines;
    for (const OutlineItem& item : items) {
        const VertexColor color = vertexColor(item.color);
        for (std::size_t i = 1; i < item.points.size(); ++i) {
            lines.push_back({.start = relativePosition(item.points[i - 1], origin),
                             .end = relativePosition(item.points[i], origin),
                             .color = color,
                             .width = static_cast<float>(kHighlightEdgeWidth),
                             .pattern = static_cast<float>(kSolidPattern)});
        }
    }
    return lines;
}

} // namespace kalve137::render
