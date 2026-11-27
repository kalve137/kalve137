// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-FileCopyrightText: 1999-2014 OPEN CASCADE SAS
// SPDX-License-Identifier: GPL-3.0-or-later AND LGPL-2.1-only WITH OCCT-exception-1.0

#include "camera/gravity.h"

#include <algorithm>
#include <cmath>

namespace kalve137::camera {

Vec3 gravityPoint(const Camera& camera, std::span<const Box> boxes)
{
    Vec3 sum;
    int count = 0;
    for (const Box& box : boxes) {
        for (const Vec3& corner : box.corners()) {
            const Vec3 projected = camera.project(corner);
            if (std::abs(projected.x) <= 1.0 && std::abs(projected.y) <= 1.0) {
                sum = sum + corner;
                ++count;
            }
        }
    }
    if (count > 0) {
        return sum / count;
    }
    if (boxes.empty()) {
        return {};
    }
    Box all = boxes.front();
    for (const Box& box : boxes) {
        all.min = {std::min(all.min.x, box.min.x), std::min(all.min.y, box.min.y), std::min(all.min.z, box.min.z)};
        all.max = {std::max(all.max.x, box.max.x), std::max(all.max.y, box.max.y), std::max(all.max.z, box.max.z)};
    }
    return all.center();
}

} // namespace kalve137::camera
