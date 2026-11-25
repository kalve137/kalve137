// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/body_opacity.h"

#include "viewer/style.h"

#include <memory>
#include <utility>
#include <vector>

namespace kalve137::app {

namespace {

constexpr float kPlanesModeOpacity = 1.0F - static_cast<float>(viewer::style::kTintHalf);
constexpr float kPreviewOpacity = 1.0F - static_cast<float>(viewer::style::kTintHalf);

void setOpacity(std::vector<std::shared_ptr<const snapshot::Body>>& bodies, PickMode mode)
{
    for (std::shared_ptr<const snapshot::Body>& body : bodies) {
        if (body == nullptr) {
            continue;
        }
        const float opacity = bodyOpacity(mode, body->ghost);
        if (body->opacity == opacity) {
            continue;
        }
        snapshot::Body changed = *body;
        changed.opacity = opacity;
        body = std::make_shared<const snapshot::Body>(std::move(changed));
    }
}

} // namespace

float bodyOpacity(PickMode mode, bool ghost)
{
    if (ghost) {
        return kPreviewOpacity;
    }
    return mode == PickMode::Planes ? kPlanesModeOpacity : 1.0F;
}

snapshot::Scene withBodyOpacity(snapshot::Scene scene, PickMode mode)
{
    setOpacity(scene.bodies, mode);
    setOpacity(scene.hidden, mode);
    return scene;
}

} // namespace kalve137::app
