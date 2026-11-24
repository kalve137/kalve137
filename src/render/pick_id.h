// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace kalve137::render {

enum class PickKind : std::uint8_t { None, Face, Edge, Point };

struct PickKinds {
    bool faces = true;
    bool edges = true;
    bool points = false;
    bool planes = false;
    bool axes = false;
    bool markers = false;
    bool sketchLines = false;
    bool sketchPoints = false;
    bool profiles = false;

    bool has(PickKind kind) const;
    bool allows(std::uint32_t id) const;
    bool any() const
    {
        return faces || edges || points || planes || axes || markers || sketchLines || sketchPoints || profiles;
    }
    bool operator==(const PickKinds&) const = default;
};

inline constexpr std::uint32_t kPickChunks = 1U << 14;
inline constexpr std::uint32_t kSketchChunk = kPickChunks - 4;
inline constexpr std::uint32_t kMarkerChunk = kPickChunks - 3;
inline constexpr std::uint32_t kOriginChunk = kPickChunks - 2;
inline constexpr std::uint32_t kDatumChunk = kPickChunks - 1;
inline constexpr std::uint32_t kBodyChunks = kSketchChunk;
inline constexpr unsigned kPickKindShift = 16;
inline constexpr unsigned kPickChunkShift = 18;

struct PickId {
    std::uint32_t chunk = 0;
    PickKind kind = PickKind::None;
    std::uint32_t entry = 0;
};

std::uint32_t encodePickId(std::uint32_t chunk, PickKind kind, std::uint32_t entry);
PickId decodePickId(std::uint32_t id);

inline constexpr double kPickEdgeWidth = 1.5;
inline constexpr double kPickPointWidth = 6.0;
inline constexpr double kPickMarkerWidth = 12.0;
inline constexpr int kPickTolerance = 4;
inline constexpr int kDatumPickTolerance = 2;
inline constexpr int kAxisPickTolerance = 3;

struct IdStyle {
    bool points = false;
    bool planes = false;
    bool axes = false;
    bool datumsThroughBodies = false;
    bool markers = false;
    bool sketchLines = false;
    bool sketchPoints = false;
    bool profiles = false;
    double edgeWidth = kPickEdgeWidth;
    double pointWidth = kPickPointWidth;
    double markerWidth = kPickMarkerWidth;

    bool operator==(const IdStyle&) const = default;
};

struct IdImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> ids;
    std::vector<float> depths;

    std::size_t indexOf(int x, int y) const
    {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x);
    }
    std::uint32_t idAt(int x, int y) const { return ids[indexOf(x, y)]; }
    float depthAt(int x, int y) const { return depths[indexOf(x, y)]; }
};

struct IdHit {
    std::uint32_t id = 0;
    int x = 0;
    int y = 0;
    float depth = 0.0F;
};

std::optional<IdHit> resolvePick(const IdImage& image, int x, int y, int radius, const PickKinds& kinds,
                                 int datumRadius = 0, int axisRadius = 0, std::span<const std::uint32_t> exactIds = {});

enum class DepthReach : std::uint8_t { InsideFar, PastFar };

std::optional<camera::Vec3> pointAtDepth(camera::Camera camera, const camera::Viewport& viewport, int x, int y,
                                         float depth, DepthReach reach);

} // namespace kalve137::render
