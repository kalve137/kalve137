// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/cpu_backend.h"

#include "render/cpu_backend_detail.h"
#include "render/edge_quad.h"
#include "render/style.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>

namespace kalve137::render {

using namespace cpu_backend_detail;

void CpuBackend::project(const DrawList& list)
{
    m_vertices.clear();
    m_triangles.clear();
    m_edgeColor = vertexColor(list.edgeColor);
    m_highlightedEdgesOnly = list.highlightedEdgesOnly;
    m_edgePixels = list.edgeWidth * list.pixelRatio;
    m_highlightPixels = kHighlightEdgeWidth * list.pixelRatio;
    m_haloPixels = kHaloWidth * list.pixelRatio;
    m_dash = static_cast<float>(kHiddenDash * list.pixelRatio);
    m_gap = static_cast<float>(kHiddenGap * list.pixelRatio);
    const HighlightDraw& highlight = list.highlight;
    for (const Chunk& item : list.chunks) {
        if (list.faces) {
            projectFaces(list, item, 0);
        }
    }
    if (highlight.wireFill) {
        projectHighlight(list, highlight.selectedFaces, Pass::GoldFill, kWireFaceWeight, 0.0F);
        projectHighlight(list, highlight.hoveredFaces, Pass::MaskFill, kWireFaceWeight, 0.0F);
    }
    projectLines(list, highlight.lines, m_highlightPixels + (2.0 * m_haloPixels), Pass::Halo);
    for (const Chunk& item : list.chunks) {
        if (list.edges) {
            projectSegments(list, item.segments, item.edgePalette, m_edgePixels, 0);
        }
    }
    projectGrid(list);
    projectDatums(list);
    if (highlight.hidden) {
        projectHighlight(list, highlight.selectedFaces, Pass::HiddenFace, kHiddenFaceWeight, list.edges ? 0.0F : -1.0F);
        projectLines(list, highlight.selectedEdges, m_highlightPixels, Pass::HiddenEdge);
    }
    projectOrigin(list);
}

void CpuBackend::projectHighlight(const DrawList& list, std::span<const Position> faces, Pass pass, float weight,
                                  float bias)
{
    const std::array<float, 16>& m = list.frame.viewProjection;
    const double width = static_cast<double>(m_width) * m_supersample;
    const double height = static_cast<double>(m_height) * m_supersample;
    const VertexColor gold = vertexColor(kSelectedGold);
    for (std::size_t i = 0; i + 2 < faces.size(); i += 3) {
        const auto first = static_cast<std::uint32_t>(m_vertices.size());
        for (std::size_t k = 0; k < 3; ++k) {
            const Position& p = faces[i + k];
            const auto row = [&](std::size_t r) {
                return (static_cast<double>(m[r]) * p[0]) + (static_cast<double>(m[r + 4]) * p[1]) +
                       (static_cast<double>(m[r + 8]) * p[2]) + static_cast<double>(m[r + 12]);
            };
            const double w = row(3);
            Projected out;
            out.valid = w > 0.0;
            if (out.valid) {
                out.x = fixed((row(0) / w + 1.0) * 0.5 * width);
                out.y = fixed((1.0 - row(1) / w) * 0.5 * height);
                out.z = static_cast<float>(row(2) / w);
            }
            m_vertices.push_back(out);
        }
        m_triangles.push_back({.a = first,
                               .b = first + 1,
                               .c = first + 2,
                               .color = gold,
                               .linear = {},
                               .pass = pass,
                               .hovered = false,
                               .selected = false,
                               .weight = weight,
                               .cull = false,
                               .flat = true,
                               .biasSlope = bias * kFaceBiasSlope,
                               .biasUnits = bias * static_cast<float>(kFaceBiasUnits),
                               .alongX = false,
                               .id = 0,
                               .pattern = 0});
    }
}

void CpuBackend::projectLines(const DrawList& list, std::span<const Segment> segments, double widthPixels, Pass pass)
{
    const VertexColor gold = vertexColor(kSelectedGold);
    for (const Segment& segment : segments) {
        addQuad(list, segment, widthPixels,
                {.a = 0,
                 .b = 0,
                 .c = 0,
                 .color = gold,
                 .linear = {},
                 .pass = pass,
                 .hovered = false,
                 .selected = false,
                 .weight = 0.0F,
                 .cull = false,
                 .flat = true,
                 .biasSlope = 0.0F,
                 .biasUnits = 0.0F,
                 .alongX = false,
                 .id = 0,
                 .pattern = 0});
    }
}

void CpuBackend::projectFaces(const DrawList& list, const Chunk& item, std::uint32_t id)
{
    const std::array<float, 16>& m = list.frame.viewProjection;
    const double width = static_cast<double>(m_width) * m_supersample;
    const double height = static_cast<double>(m_height) * m_supersample;
    const int chunks = m_pool.size() * kChunksPerThread;
    const std::span<const Vertex> vertices = item.vertices;
    const auto offset = static_cast<std::uint32_t>(m_vertices.size());
    m_vertices.resize(m_vertices.size() + vertices.size());
    m_pool.run(chunks, [&](int chunk, int) {
        const std::size_t last = chunkStart(vertices.size(), chunk + 1, chunks);
        for (std::size_t i = chunkStart(vertices.size(), chunk, chunks); i < last; ++i) {
            const Position& p = vertices[i].position;
            const auto row = [&](int r) {
                const auto index = static_cast<std::size_t>(r);
                return (static_cast<double>(m[index]) * p[0]) + (static_cast<double>(m[index + 4]) * p[1]) +
                       (static_cast<double>(m[index + 8]) * p[2]) + static_cast<double>(m[index + 12]);
            };
            const double w = row(3);
            Projected& out = m_vertices[offset + i];
            out.valid = w > 0.0;
            if (!out.valid) {
                continue;
            }
            out.x = fixed((row(0) / w + 1.0) * 0.5 * width);
            out.y = fixed((1.0 - row(1) / w) * 0.5 * height);
            out.z = static_cast<float>(row(2) / w);
            out.normal = decodeNormal(vertices[i].packed[0], vertices[i].packed[1]);
            out.position = p;
        }
    });
    const bool cull = list.cullClosed && item.closed;
    const std::span<const std::uint32_t> indices = item.indices;
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() || indices[i + 2] >= vertices.size()) {
            continue;
        }
        const std::uint32_t entry = entryOf(vertices[indices[i]]);
        if (entry >= item.palette.size() || (item.palette[entry][3] & kEntryVisible) == 0) {
            continue;
        }
        const Entry& value = item.palette[entry];
        const VertexColor color{value[0], value[1], value[2], 255};
        m_triangles.push_back({.a = offset + indices[i],
                               .b = offset + indices[i + 1],
                               .c = offset + indices[i + 2],
                               .color = color,
                               .linear = linearOf(color),
                               .pass = Pass::Face,
                               .hovered = !m_ids && (value[3] & kEntryHovered) != 0,
                               .selected = !m_ids && (value[3] & kEntrySelected) != 0,
                               .weight = 0.0F,
                               .cull = cull,
                               .flat = !list.lit,
                               .biasSlope = list.edges ? kFaceBiasSlope : 0.0F,
                               .biasUnits = list.edges ? static_cast<float>(kFaceBiasUnits) : 0.0F,
                               .alongX = false,
                               .id = id | entry,
                               .pattern = 0});
    }
}

void CpuBackend::projectSegments(const DrawList& list, std::span<const Segment> segments,
                                 std::span<const Entry> palette, double widthPixels, std::uint32_t id)
{
    const Encoded gold = encodedOf(kSelectedGold);
    const Encoded edge = encodedOf(list.edgeColor);
    for (const Segment& segment : segments) {
        const std::uint32_t entry = entryOf(segment);
        if (entry >= palette.size() || (palette[entry][3] & kEntryVisible) == 0) {
            continue;
        }
        const auto state = static_cast<EdgeState>(palette[entry][0]);
        if (!m_ids && m_highlightedEdgesOnly && state == EdgeState::Normal) {
            continue;
        }
        const bool selected = state == EdgeState::Selected || state == EdgeState::SelectedHovered;
        const bool hovered = state == EdgeState::Hovered || state == EdgeState::SelectedHovered;
        Encoded shown = selected ? gold : edge;
        if (hovered) {
            shown = pushAway(shown, kHoverStep);
        }
        const VertexColor color =
            m_ids ? m_edgeColor : vertexColor({.red = shown[0], .green = shown[1], .blue = shown[2], .alpha = 1.0F});
        const bool wide = !m_ids && state != EdgeState::Normal;
        addQuad(list, segment, wide ? m_highlightPixels : widthPixels,
                {.a = 0,
                 .b = 0,
                 .c = 0,
                 .color = color,
                 .linear = linearOf(color),
                 .pass = Pass::Edge,
                 .hovered = false,
                 .selected = false,
                 .weight = 0.0F,
                 .cull = false,
                 .flat = true,
                 .biasSlope = 0.0F,
                 .biasUnits = 0.0F,
                 .alongX = false,
                 .id = id | entry,
                 .pattern = 0});
    }
}

bool CpuBackend::addQuad(const DrawList& list, const Segment& segment, double widthPixels, const Triangle& quad)
{
    const std::array<float, 16>& m = list.frame.viewProjection;
    const double width = static_cast<double>(m_width) * m_supersample;
    const double height = static_cast<double>(m_height) * m_supersample;
    const auto clipOf = [&m](const Position& p) {
        Clip clip{};
        for (std::size_t r = 0; r < clip.size(); ++r) {
            clip[r] = (static_cast<double>(m[r]) * p[0]) + (static_cast<double>(m[r + 4]) * p[1]) +
                      (static_cast<double>(m[r + 8]) * p[2]) + static_cast<double>(m[r + 12]);
        }
        return clip;
    };
    const Clip start = clipOf(segment.start);
    const Clip end = clipOf(segment.end);
    std::array<std::array<double, 3>, kEdgeCorners> corners{};
    for (int corner = 0; corner < kEdgeCorners; ++corner) {
        const std::optional<Clip> at = expandEdge(start, end, corner, widthPixels * m_supersample, width, height);
        if (!at.has_value() || (*at)[3] <= 0.0) {
            return false;
        }
        const Clip& c = *at;
        corners.at(static_cast<std::size_t>(corner)) = {(c[0] / c[3] + 1.0) * 0.5 * width,
                                                        (1.0 - c[1] / c[3]) * 0.5 * height, c[2] / c[3]};
    }
    const double half = widthPixels * 0.5;
    const double quadLength = std::hypot(corners[2][0] + corners[3][0] - corners[0][0] - corners[1][0],
                                         corners[2][1] + corners[3][1] - corners[0][1] - corners[1][1]) *
                              0.5 / m_supersample;
    const auto span = static_cast<float>(std::max(quadLength - widthPixels, 0.0));
    const auto first = static_cast<std::uint32_t>(m_vertices.size());
    for (int corner = 0; corner < kEdgeCorners; ++corner) {
        const std::array<double, 3>& c = corners.at(static_cast<std::size_t>(corner));
        const float side = (corner % 2) == 0 ? -1.0F : 1.0F;
        m_vertices.push_back({.x = fixed(c[0]),
                              .y = fixed(c[1]),
                              .z = static_cast<float>(c[2]),
                              .normal = {},
                              .position = {},
                              .tint = {},
                              .across = side * static_cast<float>(half),
                              .along = corner >= 2 ? span + static_cast<float>(half) : -static_cast<float>(half),
                              .span = span,
                              .valid = true});
    }
    const double dx = std::abs((end[0] / end[3]) - (start[0] / start[3])) * width;
    const double dy = std::abs((end[1] / end[3]) - (start[1] / start[3])) * height;
    Triangle lower = quad;
    lower.alongX = dx >= dy;
    lower.a = first;
    lower.b = first + 1;
    lower.c = first + 2;
    Triangle upper = lower;
    upper.a = first + 1;
    upper.b = first + 3;
    upper.c = first + 2;
    m_triangles.push_back(lower);
    m_triangles.push_back(upper);
    return true;
}

} // namespace kalve137::render
