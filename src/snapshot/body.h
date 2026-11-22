// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/mesh.h"

#include <memory>
#include <optional>
#include <vector>

namespace kalve137::snapshot {

struct Color {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;

    bool operator==(const Color&) const = default;
};

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
};

struct Scene {
    std::vector<std::shared_ptr<const Body>> bodies;
    Point origin{};
};

} // namespace kalve137::snapshot
