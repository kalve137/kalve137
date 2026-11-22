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

struct Vertex {
    Position position{};
    std::array<std::uint8_t, 4> packed{};
};
static_assert(sizeof(Vertex) == 16);

struct Segment {
    Position start{};
    Position end{};
    std::array<std::uint8_t, 4> packed{};
};
static_assert(sizeof(Segment) == 28);

using Entry = std::array<std::uint8_t, 4>;
inline constexpr std::uint8_t kEntryVisible = 1;
inline constexpr std::uint32_t kPaletteWidth = 256;
inline constexpr std::uint32_t kPaletteEntries = 1U << 16;

enum class EdgeState : std::uint8_t { Normal, Hovered, Selected };

struct Chunk {
    std::uint64_t id = 0;
    std::uint64_t meshRevision = 0;
    std::uint64_t paletteRevision = 0;
    bool closed = false;
    std::span<const Vertex> vertices;
    std::span<const std::uint32_t> indices;
    std::span<const Entry> palette;
    std::span<const Segment> segments;
    std::span<const Entry> edgePalette;
    std::span<const Segment> points;
    std::span<const Entry> pointPalette;
};

struct DrawList {
    Frame frame;
    bool lit = true;
    bool cullClosed = true;
    double pixelRatio = 1.0;
    std::vector<Chunk> chunks;
    bool faces = true;
    bool edges = false;
    snapshot::Color edgeColor{};
    double edgeWidth = 0.0;
};

Segment packSegment(const Position& start, const Position& end, std::uint32_t entry);
std::uint32_t entryOf(const Segment& segment);

Frame makeFrame(const camera::Camera& camera, const camera::Viewport& viewport, const snapshot::Point& origin,
                const snapshot::Color& background, const Look& look = {});
Normal viewAt(const Position& position, const Frame& frame);
Linear lit(const Normal& normal, const Normal& view, const Linear& color, const Frame& frame);
float toLinear(float encoded);
float toSrgb(float linear);
Linear linearOf(const VertexColor& color);
VertexColor vertexColor(const snapshot::Color& color);
std::array<std::uint8_t, 2> encodeNormal(const Normal& normal);
Normal decodeNormal(std::uint8_t x, std::uint8_t y);
Vertex packVertex(const Position& position, const Normal& normal, std::uint32_t entry);
std::uint32_t entryOf(const Vertex& vertex);

} // namespace kalve137::render
