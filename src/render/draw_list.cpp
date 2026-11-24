// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-FileCopyrightText: 1999-2014 OPEN CASCADE SAS
// SPDX-License-Identifier: GPL-3.0-or-later AND LGPL-2.1-only WITH OCCT-exception-1.0

#include "render/draw_list.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace kalve137::render {

namespace {

constexpr double kLightFromUp = 0.6;
constexpr double kLightFromRight = 0.35;
constexpr float kByte = 255.0F;
constexpr float kLinearLimit = 0.04045F;
constexpr float kEncodedLimit = 0.0031308F;
constexpr float kLinearSlope = 12.92F;
constexpr float kGamma = 2.4F;
constexpr float kOffset = 0.055F;

Normal toNormal(const camera::Vec3& vector)
{
    return {static_cast<float>(vector.x), static_cast<float>(vector.y), static_cast<float>(vector.z)};
}

float dot(const Normal& a, const Normal& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Normal normalized(const Normal& vector)
{
    const float length = std::sqrt(dot(vector, vector));
    if (length <= 0.0F) {
        return vector;
    }
    return {vector[0] / length, vector[1] / length, vector[2] / length};
}

std::uint8_t toByte(float value)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * kByte));
}

constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kLowByte = 0xFFU;

struct MarkerSizes {
    double ring = 0.0;
    double cross = 0.0;
    double dot = 0.0;
};

MarkerSizes sizesOf(MarkerScale scale)
{
    switch (scale) {
    case MarkerScale::Small:
        return {.ring = kMarkerSmallRing, .cross = kMarkerSmallCross, .dot = kMarkerSmallDot};
    case MarkerScale::Medium:
        return {.ring = kMarkerMediumRing, .cross = kMarkerMediumCross, .dot = kMarkerMediumDot};
    case MarkerScale::Large:
        return {.ring = kMarkerLargeRing, .cross = kMarkerLargeCross, .dot = kMarkerLargeDot};
    }
    return {};
}

} // namespace

Marker placeMarker(const MarkerItem& item, const snapshot::Point& origin)
{
    const MarkerSizes sizes = sizesOf(item.scale);
    Marker marker;
    marker.position = relativePosition({item.anchor.x, item.anchor.y, item.anchor.z}, origin);
    marker.color = vertexColor(item.color);
    for (std::size_t i = 0; i < marker.pick.size(); ++i) {
        marker.pick.at(i) = static_cast<std::uint8_t>((item.pick >> (kByteBits * i)) & kLowByte);
    }
    marker.ring = static_cast<float>(sizes.ring);
    marker.cross = static_cast<float>(sizes.cross);
    marker.dot = static_cast<float>(sizes.dot);
    marker.style = {static_cast<std::uint8_t>(item.shape), item.flags, 0, 0};
    return marker;
}

MarkerShape shapeOf(const Marker& marker)
{
    return static_cast<MarkerShape>(marker.style[0]);
}

std::uint8_t flagsOf(const Marker& marker)
{
    return marker.style[1];
}

std::uint32_t pickOf(const Marker& marker)
{
    std::uint32_t pick = 0;
    for (std::size_t i = 0; i < marker.pick.size(); ++i) {
        pick |= static_cast<std::uint32_t>(marker.pick.at(i)) << (kByteBits * i);
    }
    return pick;
}

std::vector<camera::Box> bodyBounds(const snapshot::Scene& scene)
{
    std::vector<camera::Box> boxes;
    boxes.reserve(scene.bodies.size());
    for (const auto& body : scene.bodies) {
        const snapshot::Mesh& mesh = *body->mesh;
        if (!mesh.vertices.empty()) {
            boxes.push_back({{mesh.low[0], mesh.low[1], mesh.low[2]}, {mesh.high[0], mesh.high[1], mesh.high[2]}});
        }
    }
    return boxes;
}

std::optional<camera::Box> sceneBounds(const snapshot::Scene& scene)
{
    const std::vector<camera::Box> boxes = bodyBounds(scene);
    if (boxes.empty()) {
        return std::nullopt;
    }
    camera::Box bounds = boxes.front();
    for (const camera::Box& box : boxes) {
        bounds.min = {std::min(bounds.min.x, box.min.x), std::min(bounds.min.y, box.min.y),
                      std::min(bounds.min.z, box.min.z)};
        bounds.max = {std::max(bounds.max.x, box.max.x), std::max(bounds.max.y, box.max.y),
                      std::max(bounds.max.z, box.max.z)};
    }
    return bounds;
}

std::optional<camera::Box> sketchBounds(std::span<const snapshot::Sketch> sketches)
{
    std::optional<camera::Box> bounds;
    for (const snapshot::Sketch& sketch : sketches) {
        for (const snapshot::SketchCurve& curve : sketch.curves) {
            for (const snapshot::Point& point : curve.points) {
                const camera::Vec3 at{point[0], point[1], point[2]};
                bounds = unite(bounds, camera::Box{at, at});
            }
        }
    }
    return bounds;
}

camera::Camera fitToScene(camera::Camera camera, const snapshot::Scene& scene)
{
    camera.fitDepth(sceneBounds(scene));
    return camera;
}

Frame makeFrame(const camera::Camera& camera, const camera::Viewport& viewport, const snapshot::Point& origin,
                const snapshot::Color& background, const Look& look)
{
    camera::Camera shown = camera;
    if (viewport.isValid()) {
        shown.setAspect(viewport.aspect());
    }
    const camera::Vec3 eye = -shown.direction();
    const camera::Vec3 up = shown.orthogonalizedUp();
    const camera::Vec3 right = camera::cross(shown.direction(), up);
    const camera::Vec3 eyePosition = shown.eye() - camera::Vec3{origin[0], origin[1], origin[2]};
    Frame frame;
    const camera::Mat4 viewProjection = shown.projection() * shown.viewRelativeTo({origin[0], origin[1], origin[2]});
    frame.viewProjection = viewProjection.toFloat();
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            frame.exactViewProjection.at(static_cast<std::size_t>(row) + (4 * static_cast<std::size_t>(column))) =
                viewProjection.at(row, column);
        }
    }
    frame.wideViewProjection = frame.viewProjection;
    frame.eye = toNormal(eye);
    frame.eyePosition = toNormal(eyePosition);
    frame.perspective = !shown.isOrthographic();
    frame.light = toNormal(camera::normalized(eye + up * kLightFromUp + right * kLightFromRight));
    frame.look = look;
    frame.background = background;
    frame.width = viewport.width;
    frame.height = viewport.height;
    frame.pixelRatio = viewport.pixelRatio;
    return frame;
}

std::optional<std::array<double, 3>> planeDepth(const std::array<double, 16>& viewProjection,
                                                const std::array<float, 4>& plane)
{
    constexpr double kEdgeOn = 1e-9;
    camera::Mat4 matrix;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            matrix.at(row, column) =
                viewProjection.at(static_cast<std::size_t>(row) + (4 * static_cast<std::size_t>(column)));
        }
    }
    const std::optional<camera::Mat4> inverse = matrix.inverted();
    if (!inverse) {
        return std::nullopt;
    }
    std::array<double, 4> clip{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            clip.at(static_cast<std::size_t>(column)) +=
                inverse->at(row, column) * static_cast<double>(plane.at(static_cast<std::size_t>(row)));
        }
    }
    if (std::abs(clip[2]) <= kEdgeOn * (std::abs(clip[0]) + std::abs(clip[1]) + std::abs(clip[3]))) {
        return std::nullopt;
    }
    return std::array<double, 3>{-clip[0] / clip[2], -clip[1] / clip[2], -clip[3] / clip[2]};
}

std::optional<camera::Box> unite(const std::optional<camera::Box>& a, const std::optional<camera::Box>& b)
{
    if (!a) {
        return b;
    }
    if (!b) {
        return a;
    }
    return camera::Box{{std::min(a->min.x, b->min.x), std::min(a->min.y, b->min.y), std::min(a->min.z, b->min.z)},
                       {std::max(a->max.x, b->max.x), std::max(a->max.y, b->max.y), std::max(a->max.z, b->max.z)}};
}

void widenFrame(Frame& frame, camera::Camera camera, const camera::Viewport& viewport, const snapshot::Point& origin,
                const std::optional<camera::Box>& reach)
{
    if (viewport.isValid()) {
        camera.setAspect(viewport.aspect());
    }
    const camera::Mat4 body = camera.projection();
    camera.fitDepth(reach);
    const camera::Mat4 wide = camera.projection();
    frame.wideViewProjection = (wide * camera.viewRelativeTo({origin[0], origin[1], origin[2]})).toFloat();
    if (camera.isOrthographic()) {
        const double scale = body.at(2, 2) / wide.at(2, 2);
        frame.wideDepth = {scale, body.at(2, 3) - (scale * wide.at(2, 3))};
    } else {
        const double scale = body.at(2, 3) / wide.at(2, 3);
        frame.wideDepth = {scale, (scale * wide.at(2, 2)) - body.at(2, 2)};
    }
}

Normal viewAt(const Position& position, const Frame& frame)
{
    if (!frame.perspective) {
        return frame.eye;
    }
    return normalized(
        {frame.eyePosition[0] - position[0], frame.eyePosition[1] - position[1], frame.eyePosition[2] - position[2]});
}

Linear lit(const Normal& normal, const Normal& view, const Linear& color, const Frame& frame)
{
    Normal side = normal;
    if (dot(side, view) < 0.0F) {
        side = {-side[0], -side[1], -side[2]};
    }
    const Look& look = frame.look;
    const float diffuse = std::max(0.0F, dot(side, frame.light));
    float specular = 0.0F;
    if (diffuse > 0.0F) {
        const Normal half = normalized({frame.light[0] + view[0], frame.light[1] + view[1], frame.light[2] + view[2]});
        specular = std::pow(std::max(0.0F, dot(side, half)), look.shininess);
    }
    const float level = look.ambient + (look.diffuse * diffuse);
    return {(color[0] * level) + (look.specular[0] * specular), (color[1] * level) + (look.specular[1] * specular),
            (color[2] * level) + (look.specular[2] * specular)};
}

float toLinear(float encoded)
{
    return encoded <= kLinearLimit ? encoded / kLinearSlope : std::pow((encoded + kOffset) / (1.0F + kOffset), kGamma);
}

float toSrgb(float linear)
{
    const float value = std::clamp(linear, 0.0F, 1.0F);
    return value <= kEncodedLimit ? value * kLinearSlope
                                  : ((1.0F + kOffset) * std::pow(value, 1.0F / kGamma)) - kOffset;
}

Linear linearOf(const VertexColor& color)
{
    const auto channel = [](std::uint8_t value) { return toLinear(static_cast<float>(value) / kByte); };
    return {channel(color[0]), channel(color[1]), channel(color[2])};
}

VertexColor vertexColor(const snapshot::Color& color)
{
    return {toByte(color.red), toByte(color.green), toByte(color.blue), toByte(color.alpha)};
}

std::array<std::uint8_t, 2> encodeNormal(const Normal& normal)
{
    const float sum = std::abs(normal[0]) + std::abs(normal[1]) + std::abs(normal[2]);
    if (sum <= 0.0F) {
        return {toByte(0.5F), toByte(0.5F)};
    }
    float x = normal[0] / sum;
    float y = normal[1] / sum;
    if (normal[2] < 0.0F) {
        const float foldedX = (1.0F - std::abs(y)) * (x >= 0.0F ? 1.0F : -1.0F);
        const float foldedY = (1.0F - std::abs(x)) * (y >= 0.0F ? 1.0F : -1.0F);
        x = foldedX;
        y = foldedY;
    }
    return {toByte((x * 0.5F) + 0.5F), toByte((y * 0.5F) + 0.5F)};
}

Normal decodeNormal(std::uint8_t x, std::uint8_t y)
{
    Normal normal{(static_cast<float>(x) / kByte * 2.0F) - 1.0F, (static_cast<float>(y) / kByte * 2.0F) - 1.0F, 0.0F};
    normal[2] = 1.0F - std::abs(normal[0]) - std::abs(normal[1]);
    const float fold = std::max(-normal[2], 0.0F);
    normal[0] += normal[0] >= 0.0F ? -fold : fold;
    normal[1] += normal[1] >= 0.0F ? -fold : fold;
    return normalized(normal);
}

Vertex packVertex(const Position& position, const Normal& normal, std::uint32_t entry)
{
    const std::array<std::uint8_t, 2> encoded = encodeNormal(normal);
    return {.position = position,
            .packed = {encoded[0], encoded[1], static_cast<std::uint8_t>(entry & 0xFFU),
                       static_cast<std::uint8_t>((entry >> 8) & 0xFFU)}};
}

std::uint32_t entryOf(const Vertex& vertex)
{
    return static_cast<std::uint32_t>(vertex.packed[2]) | (static_cast<std::uint32_t>(vertex.packed[3]) << 8);
}

Segment packSegment(const Position& start, const Position& end, std::uint32_t entry)
{
    return {
        .start = start,
        .end = end,
        .packed = {static_cast<std::uint8_t>(entry & 0xFFU), static_cast<std::uint8_t>((entry >> 8) & 0xFFU), 0, 0}};
}

std::uint32_t entryOf(const Segment& segment)
{
    return static_cast<std::uint32_t>(segment.packed[0]) | (static_cast<std::uint32_t>(segment.packed[1]) << 8);
}

std::optional<PixelRect> haloBounds(const DrawList& list)
{
    constexpr double kMargin = 2.0;
    constexpr float kNearW = 1e-6F;
    const HighlightDraw& highlight = list.highlight;
    const bool masked = highlight.wireFill && !highlight.hoveredFaces.empty();
    const Frame& frame = list.frame;
    if ((highlight.lines.empty() && !masked) || frame.width <= 0 || frame.height <= 0) {
        return std::nullopt;
    }
    const PixelRect whole{.x = 0, .y = 0, .width = frame.width, .height = frame.height};
    const std::array<float, 16>& m = frame.viewProjection;
    double left = std::numeric_limits<double>::max();
    double top = left;
    double right = std::numeric_limits<double>::lowest();
    double bottom = right;
    bool behind = false;
    const auto add = [&](const Position& p, double reach) {
        const float x = (m[0] * p[0]) + (m[4] * p[1]) + (m[8] * p[2]) + m[12];
        const float y = (m[1] * p[0]) + (m[5] * p[1]) + (m[9] * p[2]) + m[13];
        const float w = (m[3] * p[0]) + (m[7] * p[1]) + (m[11] * p[2]) + m[15];
        if (w < kNearW) {
            behind = true;
            return;
        }
        const double px = (x / w + 1.0) * 0.5 * frame.width;
        const double py = (1.0 - y / w) * 0.5 * frame.height;
        left = std::min(left, px - reach);
        right = std::max(right, px + reach);
        top = std::min(top, py - reach);
        bottom = std::max(bottom, py + reach);
    };
    const double lineReach = (kHighlightEdgeWidth * list.pixelRatio * 0.5) + (kHaloWidth * list.pixelRatio) + kMargin;
    for (const Segment& segment : highlight.lines) {
        add(segment.start, lineReach);
        add(segment.end, lineReach);
    }
    if (masked) {
        for (const Position& position : highlight.hoveredFaces) {
            add(position, kMargin);
        }
    }
    if (behind) {
        return whole;
    }
    const auto clamped = [](double value, int size) { return std::clamp(value, 0.0, static_cast<double>(size)); };
    const int x0 = static_cast<int>(std::floor(clamped(left, frame.width)));
    const int y0 = static_cast<int>(std::floor(clamped(top, frame.height)));
    const int x1 = static_cast<int>(std::ceil(clamped(right, frame.width)));
    const int y1 = static_cast<int>(std::ceil(clamped(bottom, frame.height)));
    if (x1 <= x0 || y1 <= y0) {
        return std::nullopt;
    }
    return PixelRect{.x = x0, .y = y0, .width = x1 - x0, .height = y1 - y0};
}

float luminanceOf(const Encoded& color)
{
    return (0.2126F * color[0]) + (0.7152F * color[1]) + (0.0722F * color[2]);
}

Encoded pushAway(const Encoded& color, float step)
{
    const float toward = luminanceOf(color) >= kNearWhite ? 0.0F : 1.0F;
    return {color[0] + ((toward - color[0]) * step), color[1] + ((toward - color[1]) * step),
            color[2] + ((toward - color[2]) * step)};
}

Encoded highlighted(const Encoded& color, bool hovered, bool selected)
{
    Encoded result = color;
    if (selected) {
        const Encoded gold = encodedOf(kSelectedGold);
        for (std::size_t c = 0; c < result.size(); ++c) {
            result.at(c) += (gold.at(c) - result.at(c)) * kSelectedMix;
        }
    }
    return hovered ? pushAway(result, kHoverStep) : result;
}

Encoded encodedOf(const snapshot::Color& color)
{
    return {color.red, color.green, color.blue};
}

} // namespace kalve137::render
