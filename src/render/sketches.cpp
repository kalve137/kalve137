// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/sketches.h"

#include "render/datums.h"
#include "render/pick_id.h"
#include "render/positions.h"
#include "render/style.h"

#include <algorithm>

namespace kalve137::render {

namespace {

constexpr float kByte = 255.0F;
constexpr std::uint32_t kLowByte = 0xFFU;
constexpr unsigned kByteBits = 8;

VertexColor idColor(std::uint32_t entry, PickKind kind)
{
    return {static_cast<std::uint8_t>(entry & kLowByte), static_cast<std::uint8_t>((entry >> kByteBits) & kLowByte),
            static_cast<std::uint8_t>(kind), static_cast<std::uint8_t>(kByte)};
}

snapshot::Color opaque(snapshot::Color color)
{
    color.alpha = 1.0F;
    return color;
}

class Builder {
public:
    Builder(const SketchLook& look, const SketchMarks& marks, const snapshot::Point& origin, SketchGeometry& geometry)
        : m_look(look), m_marks(marks), m_origin(origin), m_geometry(geometry)
    {}

    void add(const snapshot::Sketch& sketch)
    {
        for (const snapshot::SketchCurve& curve : sketch.curves) {
            addCurve(sketch.feature, curve);
        }
        if (m_look.points) {
            for (const snapshot::SketchPoint& point : sketch.points) {
                addPoint(sketch.feature, point);
            }
        }
        if (m_look.profiles) {
            addProfiles(sketch.feature, sketch.profiles);
        }
    }

private:
    std::optional<std::uint32_t> enter(const SketchKey& key)
    {
        if (m_geometry.keys.size() >= kDatumEntries) {
            m_geometry.capped = true;
            return std::nullopt;
        }
        m_geometry.keys.push_back(key);
        return static_cast<std::uint32_t>(m_geometry.keys.size() - 1);
    }

    snapshot::Color marked(const SketchKey& key, const snapshot::Color& base) const
    {
        if (m_marks.hovered == key) {
            return opaque(m_look.hover);
        }
        return m_marks.selected.contains(key) ? opaque(m_look.highlight) : base;
    }

    void addCurve(int feature, const snapshot::SketchCurve& curve)
    {
        const SketchKey key{.item = SketchItem::Line, .feature = feature, .index = curve.id};
        const snapshot::Color color = marked(key, curve.construction ? m_look.construction : m_look.free);
        const double width = curve.construction ? kSketchConstructionWidth : kSketchCurveWidth;
        const std::uint32_t pattern = curve.construction ? kDashPattern : kSolidPattern;
        const bool pickable = m_look.lines && curve.kind == snapshot::SketchCurveKind::Line;
        const std::optional<std::uint32_t> entry = pickable ? enter(key) : std::nullopt;
        for (std::size_t i = 1; i < curve.points.size(); ++i) {
            const Position start = relativePosition(curve.points[i - 1], m_origin);
            const Position end = relativePosition(curve.points[i], m_origin);
            m_geometry.curves.push_back({.start = start,
                                         .end = end,
                                         .color = vertexColor(color),
                                         .width = static_cast<float>(width),
                                         .pattern = static_cast<float>(pattern)});
            if (entry) {
                m_geometry.idCurves.push_back({.start = start,
                                               .end = end,
                                               .color = idColor(*entry, PickKind::Edge),
                                               .width = static_cast<float>(std::max(width, kPickEdgeWidth)),
                                               .pattern = static_cast<float>(kSolidPattern)});
            }
        }
    }

    void addPoint(int feature, const snapshot::SketchPoint& point)
    {
        const SketchKey key{.item = SketchItem::Point, .feature = feature, .index = point.id};
        const std::optional<std::uint32_t> entry = enter(key);
        m_geometry.markers.push_back(
            {.anchor = {point.point[0], point.point[1], point.point[2]},
             .shape = MarkerShape::Ring,
             .scale = MarkerScale::Small,
             .color = marked(key, m_look.free),
             .flags = static_cast<std::uint8_t>(kMarkerTopmost | (entry ? kMarkerPickable : 0)),
             .pick = entry ? encodePickId(kSketchChunk, PickKind::Point, *entry) : 0});
    }

    void addProfiles(int feature, const snapshot::SketchProfiles& profiles)
    {
        std::vector<std::optional<std::uint32_t>> entries(profiles.count);
        std::vector<VertexColor> colors(profiles.count);
        for (std::uint32_t index = 0; index < profiles.count; ++index) {
            const SketchKey key{.item = SketchItem::Profile, .feature = feature, .index = static_cast<int>(index)};
            entries[index] = enter(key);
            if (entries[index] && std::ranges::find(profiles.disks, index) != profiles.disks.end()) {
                m_geometry.exactIds.push_back(encodePickId(kSketchChunk, PickKind::Face, *entries[index]));
            }
            const bool lit = m_look.highlighted.contains({feature, index}) || m_marks.selected.contains(key);
            colors[index] = vertexColor(m_marks.hovered == key ? m_look.hover
                                        : lit                  ? m_look.highlight
                                                               : m_look.profile);
        }
        const std::size_t count = std::min(profiles.triangles.size(), profiles.profiles.size());
        for (std::size_t t = 0; t < count; ++t) {
            const std::uint32_t profile = profiles.profiles[t];
            if (profile >= profiles.count) {
                continue;
            }
            const snapshot::Triangle& triangle = profiles.triangles[t];
            if (std::ranges::any_of(triangle, [&](std::uint32_t at) { return at >= profiles.vertices.size(); })) {
                continue;
            }
            for (const std::uint32_t at : triangle) {
                const Position position = relativePosition(profiles.vertices[at], m_origin);
                m_geometry.profiles.push_back({.position = position, .color = colors[profile]});
                if (entries[profile]) {
                    m_geometry.idProfiles.push_back(
                        {.position = position, .color = idColor(*entries[profile], PickKind::Face)});
                }
            }
        }
    }

    const SketchLook& m_look;
    const SketchMarks& m_marks;
    snapshot::Point m_origin;
    SketchGeometry& m_geometry;
};

} // namespace

SketchGeometry buildSketches(std::span<const snapshot::Sketch> sketches, const SketchLook& look,
                             const SketchMarks& marks, const snapshot::Point& origin)
{
    SketchGeometry geometry;
    Builder builder(look, marks, origin, geometry);
    for (const snapshot::Sketch& sketch : sketches) {
        if (look.editing == 0 || sketch.feature != look.editing) {
            builder.add(sketch);
        }
    }
    return geometry;
}

ActiveGeometry placeActiveSketch(const ActiveSketch& sketch, const snapshot::Point& origin)
{
    ActiveGeometry geometry;
    const snapshot::SketchProfiles& profiles = sketch.profiles;
    const VertexColor profileColor = vertexColor(sketch.profileColor);
    const std::size_t count = std::min(profiles.triangles.size(), profiles.profiles.size());
    for (std::size_t t = 0; t < count; ++t) {
        const snapshot::Triangle& triangle = profiles.triangles[t];
        if (profiles.profiles[t] >= profiles.count ||
            std::ranges::any_of(triangle, [&](std::uint32_t at) { return at >= profiles.vertices.size(); })) {
            continue;
        }
        for (const std::uint32_t at : triangle) {
            geometry.profiles.push_back(
                {.position = relativePosition(profiles.vertices[at], origin), .color = profileColor});
        }
    }
    for (const SketchStroke& stroke : sketch.strokes) {
        const VertexColor color = vertexColor(stroke.color);
        for (std::size_t i = 1; i < stroke.points.size(); ++i) {
            geometry.lines.push_back({.start = relativePosition(stroke.points[i - 1], origin),
                                      .end = relativePosition(stroke.points[i], origin),
                                      .color = color,
                                      .width = static_cast<float>(stroke.width),
                                      .pattern = static_cast<float>(stroke.pattern)});
        }
    }
    for (const SketchTriangle& arrow : sketch.arrows) {
        const VertexColor color = vertexColor(arrow.color);
        for (const snapshot::Point& corner : arrow.corners) {
            geometry.arrows.push_back({.position = relativePosition(corner, origin), .color = color});
        }
    }
    return geometry;
}

std::optional<SketchKey> sketchAt(const SketchGeometry& geometry, std::uint32_t entry)
{
    if (entry >= geometry.keys.size()) {
        return std::nullopt;
    }
    return geometry.keys[entry];
}

std::optional<snapshot::Point> sketchPointOf(std::span<const snapshot::Sketch> sketches, const SketchKey& key)
{
    if (key.item != SketchItem::Point) {
        return std::nullopt;
    }
    for (const snapshot::Sketch& sketch : sketches) {
        if (sketch.feature != key.feature) {
            continue;
        }
        for (const snapshot::SketchPoint& point : sketch.points) {
            if (point.id == key.index) {
                return point.point;
            }
        }
    }
    return std::nullopt;
}

} // namespace kalve137::render
