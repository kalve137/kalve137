// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/document.h"
#include "core/recompute.h"
#include "render/sketches.h"
#include "snapshot/sketch.h"

#include <vector>

namespace kalve137::app {

class DesignSession;
class Scene;

std::vector<snapshot::Sketch> sketchSnapshots(const core::Document& document, const core::Model& model,
                                              const DesignSession& session);
render::SketchLook sketchLook(const Scene& scene);

} // namespace kalve137::app
