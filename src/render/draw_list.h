// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "render/positions.h"
#include "render/style.h"
#include "snapshot/mesh.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kalve137::render {

using Normal = std::array<float, 3>;
using Linear = std::array<float, 3>;
using VertexColor = std::array<std::uint8_t, 4>;

struct Look {
    float ambient = 0.0875F;
    float diffuse = 1.0F;
    Linear specular{0.992F, 0.941F, 0.808F};
    float shininess = 83.2F;

    bool operator==(const Look&) const = default;
};

struct Frame {
    std::array<float, 16> viewProjection{};
    Normal eye{0.0F, 0.0F, 1.0F};
    Position eyePosition{};
    bool perspective = false;
    Normal light{0.0F, 0.0F, 1.0F};
    Look look;
    snapshot::Color background;
    int width = 0;
    int height = 0;
    double pixelRatio = 1.0;
};

enum class ItemKind { Faces, Edges, Points };

template <typename T>
struct Stream {
    std::uint64_t id = 0;
    std::span<const T> data;
};

struct Batch {
    ItemKind kind = ItemKind::Faces;
    bool lit = true;
    Stream<Position> positions;
    Stream<Normal> normals;
    Stream<snapshot::Triangle> triangles;
    Stream<VertexColor> colors;
};

struct DrawList {
    Frame frame;
    std::vector<Batch> batches;
};

Frame makeFrame(const camera::Camera& camera, const camera::Viewport& viewport, const snapshot::Point& origin,
                const snapshot::Color& background, const Look& look = {});
Normal viewAt(const Position& position, const Frame& frame);
Linear lit(const Normal& normal, const Normal& view, const Linear& color, const Frame& frame);
float toLinear(float encoded);
float toSrgb(float linear);
Linear linearOf(const VertexColor& color);
VertexColor vertexColor(const snapshot::Color& color);

} // namespace kalve137::render
