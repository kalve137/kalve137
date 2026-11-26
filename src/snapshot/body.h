// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/color.h"
#include "snapshot/mesh.h"

#include <memory>
#include <optional>
#include <vector>

namespace kalve137::snapshot {

struct Appearance {
    std::optional<Color> color;
    std::vector<std::optional<Color>> faces;

    bool operator==(const Appearance&) const = default;
};

struct Body {
    int feature = 0;
    int part = 0;
    std::shared_ptr<const Mesh> mesh;
    Appearance appearance;
    float opacity = 1.0F;
};

struct Scene {
    std::vector<std::shared_ptr<const Body>> bodies;
    Point origin{};
    std::vector<std::shared_ptr<const Body>> hidden;
};

} // namespace kalve137::snapshot
