// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "model/snapshot.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kalve137::look {

struct View {
    double yaw = -0.785398;
    double pitch = 0.615480;
    double zoom = 1.0;
    double panX = 0.0;
    double panY = 0.0;
    double fitSpan = 0.0;
    model::Vec3 fitCenter;
};

View fitted(const model::Snapshot& snapshot, View view);

std::optional<View> namedView(const std::string& name);

struct Style {
    std::set<int> highlighted;
    int hovered = -1;
    bool edges = true;
};

struct Frame {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
    std::vector<int> faces;
    std::vector<float> depth;
    model::Vec3 center;
    model::Vec3 eye;
    model::Vec3 right;
    model::Vec3 up;
    double scale = 1.0;

    int faceAt(int x, int y) const;
    model::Vec3 project(model::Vec3 point) const;
};

Frame render(const model::Snapshot& snapshot, const View& view, int width, int height, const Style& style = {});

} // namespace kalve137::look
