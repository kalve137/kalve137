// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "render/chunk_set.h"
#include "render/pick_id.h"
#include "snapshot/body.h"

#include <memory>
#include <vector>

namespace kalve137::render {

struct ScreenRect {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    static ScreenRect spanning(camera::ScreenPoint from, camera::ScreenPoint to);
    bool contains(camera::ScreenPoint point) const;
};

bool isCrossingBox(camera::ScreenPoint from, camera::ScreenPoint to);

std::vector<PickedItem> boxCandidates(const std::vector<std::shared_ptr<const snapshot::Body>>& bodies,
                                      const camera::Camera& camera, const camera::Viewport& viewport,
                                      const ScreenRect& rect, bool crossing, const PickKinds& kinds);

} // namespace kalve137::render
