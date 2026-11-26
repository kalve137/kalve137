// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/measure.h"

#include "render/positions.h"
#include "render/style.h"

namespace kalve137::render {

std::vector<DatumLine> placeMeasureLines(const MeasureItems& items, const snapshot::Point& origin)
{
    if (!items.line) {
        return {};
    }
    const MeasureLine& line = *items.line;
    return {{.start = relativePosition({line.from.x, line.from.y, line.from.z}, origin),
             .end = relativePosition({line.to.x, line.to.y, line.to.z}, origin),
             .color = vertexColor(line.color),
             .width = static_cast<float>(kMeasureLineWidth),
             .pattern = static_cast<float>(kDashPattern)}};
}

} // namespace kalve137::render
