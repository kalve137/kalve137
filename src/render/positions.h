// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/mesh.h"

#include <array>
#include <vector>

namespace kalve137::render {

using Position = std::array<float, 3>;

inline Position relativePosition(const snapshot::Point& point, const snapshot::Point& origin)
{
    return {static_cast<float>(point[0] - origin[0]), static_cast<float>(point[1] - origin[1]),
            static_cast<float>(point[2] - origin[2])};
}

inline std::vector<Position> relativePositions(const snapshot::Mesh& mesh, const snapshot::Point& origin)
{
    std::vector<Position> positions;
    positions.reserve(mesh.vertices.size());
    for (const snapshot::Point& vertex : mesh.vertices) {
        positions.push_back(relativePosition(vertex, origin));
    }
    return positions;
}

} // namespace kalve137::render
