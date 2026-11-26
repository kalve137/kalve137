// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "render/annotations.h"
#include "render/positions.h"
#include "render/style.h"
#include "snapshot/body.h"
#include "snapshot/mesh.h"
#include "snapshot/sketch.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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
    std::array<double, 16> exactViewProjection{};
    std::array<float, 16> wideViewProjection{};
    std::array<double, 2> wideDepth{1.0, 0.0};
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
inline constexpr std::uint8_t kEntryHovered = 2;
inline constexpr std::uint8_t kEntrySelected = 4;
inline constexpr std::uint32_t kPaletteWidth = 256;
inline constexpr std::uint32_t kPaletteEntries = 1U << 16;

enum class EdgeState : std::uint8_t { Normal, Hovered, Selected, SelectedHovered };

struct BodyRange {
    Position center{};
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t firstSegment = 0;
    std::uint32_t segmentCount = 0;

    bool operator==(const BodyRange&) const = default;
};

struct Chunk {
    std::uint64_t id = 0;
    std::uint64_t meshRevision = 0;
    std::uint64_t paletteRevision = 0;
    bool closed = false;
    float opacity = 1.0F;
    std::span<const Vertex> vertices;
    std::span<const std::uint32_t> indices;
    std::span<const Entry> palette;
    std::span<const Segment> segments;
    std::span<const Entry> edgePalette;
    std::span<const Segment> points;
    std::span<const Entry> pointPalette;
    std::span<const BodyRange> ranges;

    bool transparent() const { return opacity < 1.0F; }
};

struct TransparentDraw {
    std::size_t chunk = 0;
    std::size_t range = 0;
    double farness = 0.0;
};

double farness(const Position& center, const Frame& frame);
float opacityOf(const snapshot::Body& body);

struct HighlightDraw {
    std::uint64_t revision = 0;
    std::span<const Segment> lines;
    std::span<const Segment> selectedEdges;
    std::span<const Position> selectedFaces;
    std::span<const Position> hoveredFaces;
    std::span<const Segment> overlayEdges;
    std::span<const Position> overlayFaces;
    std::span<const Position> overlayHoverFaces;
    bool hidden = false;
    bool wireFill = false;

    bool empty() const
    {
        return lines.empty() && selectedEdges.empty() && selectedFaces.empty() && hoveredFaces.empty() &&
               overlayEdges.empty() && overlayFaces.empty() && overlayHoverFaces.empty();
    }
};

struct OverlayVertex {
    Position position{};
    VertexColor color{};

    bool operator==(const OverlayVertex&) const = default;
};
static_assert(sizeof(OverlayVertex) == 16);

struct OverlayLine {
    Position start{};
    Position end{};
    VertexColor startColor{};
    VertexColor endColor{};
    float width = 0.0F;
};
static_assert(sizeof(OverlayLine) == 36);

struct GridDraw {
    std::uint64_t revision = 0;
    std::span<const OverlayVertex> wash;
    std::span<const OverlayLine> lines;
    Normal shift{};
    std::array<float, 4> plane{};

    bool empty() const { return wash.empty() && lines.empty(); }
};

struct DatumLine {
    Position start{};
    Position end{};
    VertexColor color{};
    float width = 0.0F;
    float pattern = 0.0F;

    bool operator==(const DatumLine&) const = default;
};
static_assert(sizeof(DatumLine) == 36);

struct DatumDraw {
    std::uint64_t revision = 0;
    std::span<const OverlayVertex> fills;
    std::span<const DatumLine> lines;
    std::span<const OverlayVertex> originFills;
    std::span<const DatumLine> originLines;
    std::span<const OverlayVertex> idFills;
    std::span<const DatumLine> idOutlines;
    std::span<const DatumLine> idLines;
    std::span<const OverlayVertex> idOriginFills;
    std::span<const DatumLine> idOriginOutlines;
    std::span<const DatumLine> idOriginLines;

    bool empty() const { return fills.empty() && lines.empty() && originFills.empty() && originLines.empty(); }
};

enum class MarkerShape : std::uint8_t { Ball, Ring, Plus, RingPlus, Star };
enum class MarkerScale : std::uint8_t { Small, Medium, Large };
inline constexpr std::uint8_t kMarkerTopmost = 1;
inline constexpr std::uint8_t kMarkerPickable = 2;

struct Marker {
    Position position{};
    VertexColor color{};
    std::array<std::uint8_t, 4> pick{};
    float ring = 0.0F;
    float cross = 0.0F;
    float dot = 0.0F;
    std::array<std::uint8_t, 4> style{};

    bool operator==(const Marker&) const = default;
};
static_assert(sizeof(Marker) == 36);

struct MarkerItem {
    camera::Vec3 anchor;
    MarkerShape shape = MarkerShape::Ball;
    MarkerScale scale = MarkerScale::Small;
    snapshot::Color color;
    std::uint8_t flags = kMarkerTopmost;
    std::uint32_t pick = 0;

    bool operator==(const MarkerItem&) const = default;
};

Marker placeMarker(const MarkerItem& item, const snapshot::Point& origin);
MarkerShape shapeOf(const Marker& marker);
std::uint8_t flagsOf(const Marker& marker);
std::uint32_t pickOf(const Marker& marker);

struct MarkerDraw {
    std::uint64_t revision = 0;
    std::span<const Marker> markers;

    bool empty() const { return markers.empty(); }
};

struct Handle {
    Position anchor{};
    Position toward{};
    VertexColor fill{};
    VertexColor outline{};
    VertexColor glow{};
    std::array<float, 4> arrow{};
    std::array<float, 2> stroke{};

    bool operator==(const Handle&) const = default;
};
static_assert(sizeof(Handle) == 60);

struct HandleDraw {
    std::uint64_t revision = 0;
    std::span<const Handle> arrows;
    std::span<const DatumLine> guides;
    std::span<const DatumLine> outlines;

    bool empty() const { return arrows.empty() && guides.empty() && outlines.empty(); }
};

struct SketchDraw {
    std::uint64_t revision = 0;
    std::span<const DatumLine> curves;
    std::span<const OverlayVertex> profiles;
    std::span<const DatumLine> idCurves;
    std::span<const OverlayVertex> idProfiles;

    bool empty() const { return curves.empty() && profiles.empty(); }
};

struct ActiveSketchDraw {
    std::uint64_t revision = 0;
    std::span<const OverlayVertex> profiles;
    std::span<const DatumLine> lines;
    std::span<const OverlayVertex> arrows;
    std::uint64_t previewRevision = 0;
    std::span<const DatumLine> previewLines;

    bool empty() const { return profiles.empty() && lines.empty() && arrows.empty() && previewLines.empty(); }
};

std::optional<std::array<double, 3>> planeDepth(const std::array<double, 16>& viewProjection,
                                                const std::array<float, 4>& plane);

struct DrawList {
    Frame frame;
    bool lit = true;
    bool cullClosed = true;
    double pixelRatio = 1.0;
    std::vector<Chunk> chunks;
    std::vector<TransparentDraw> transparent;
    bool faces = true;
    bool edges = false;
    bool highlightedEdgesOnly = false;
    snapshot::Color edgeColor{};
    double edgeWidth = 0.0;
    HighlightDraw highlight;
    GridDraw grid;
    DatumDraw datums;
    MarkerDraw markers;
    SketchDraw sketches;
    ActiveSketchDraw activeSketch;
    AnnotationDraw annotations;
    HandleDraw handles;
};

using Encoded = std::array<float, 3>;
float luminanceOf(const Encoded& color);
Encoded pushAway(const Encoded& color, float step);
Encoded highlighted(const Encoded& color, bool hovered, bool selected);
Encoded encodedOf(const snapshot::Color& color);

Segment packSegment(const Position& start, const Position& end, std::uint32_t entry);
std::uint32_t entryOf(const Segment& segment);

struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const PixelRect&) const = default;
};
std::optional<PixelRect> haloBounds(const DrawList& list);

std::vector<camera::Box> bodyBounds(const snapshot::Scene& scene);
std::optional<camera::Box> sceneBounds(const snapshot::Scene& scene);
std::optional<camera::Box> sketchBounds(std::span<const snapshot::Sketch> sketches);
camera::Camera fitToScene(camera::Camera camera, const snapshot::Scene& scene);

Frame makeFrame(const camera::Camera& camera, const camera::Viewport& viewport, const snapshot::Point& origin,
                const snapshot::Color& background, const Look& look = {});
std::optional<camera::Box> unite(const std::optional<camera::Box>& a, const std::optional<camera::Box>& b);
void widenFrame(Frame& frame, camera::Camera camera, const camera::Viewport& viewport, const snapshot::Point& origin,
                const std::optional<camera::Box>& reach);
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
