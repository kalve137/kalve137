// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/mesh.h"

#include <memory>
#include <vector>

namespace kalve137::snapshot {

struct Body {
    int feature = 0;
    int part = 0;
    Mesh mesh;
};

struct Scene {
    std::vector<std::shared_ptr<const Body>> bodies;
    Point origin{};
};

} // namespace kalve137::snapshot
