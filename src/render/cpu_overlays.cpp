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

void CpuBackend::projectGrid(const DrawList& list)
{
    const GridDraw& grid = list.grid;
    addFills(list, grid.wash, Pass::GridFill, kGridWashBiasSlope, kGridWashBiasUnits);
    const std::optional<std::array<double, 3>> onPlane = planeDepth(list.frame.exactViewProjection, grid.plane);
    for (const OverlayLine& line : grid.lines) {
        addGridLine(list, line, onPlane);
    }
}

void CpuBackend::projectDatums(const DrawList& list)
{
    const DatumDraw& datums = list.datums;
    m_datumDash = kDatumDashPixels * list.pixelRatio;
    for (const DatumLine& line : datums.lines) {
        addDatumLine(list, line, Pass::DatumLine);
    }
    addFills(list, datums.fills, Pass::DatumFill, kGridWashBiasSlope, kGridWashBiasUnits);
}

void CpuBackend::projectOrigin(const DrawList& list)
{
    const DatumDraw& datums = list.datums;
    if (datums.originFills.empty() && datums.originLines.empty()) {
        return;
    }
    const auto first = static_cast<std::uint32_t>(m_vertices.size());
    const int right = fixed(static_cast<double>(m_width) * m_supersample);
    const int bottom = fixed(static_cast<double>(m_height) * m_supersample);
    for (const auto& [x, y] : {std::pair{0, 0}, std::pair{right, 0}, std::pair{0, bottom}, std::pair{right, bottom}}) {
        m_vertices.push_back({.x = x,
                              .y = y,
                              .z = 0.0F,
                              .normal = {},
                              .position = {},
                              .tint = {},
                              .across = 0.0F,
                              .along = 0.0F,
                              .span = 0.0F,
                              .valid = true});
    }
    for (const std::array<std::uint32_t, 3>& corners : {std::array{0U, 1U, 2U}, std::array{1U, 3U, 2U}}) {
        m_triangles.push_back({.a = first + corners[0],
                               .b = first + corners[1],
                               .c = first + corners[2],
                               .color = {},
                               .linear = {},
                               .pass = Pass::DepthReset,
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
    for (const DatumLine& line : datums.originLines) {
        addDatumLine(list, line, Pass::OriginLine);
    }
    addFills(list, datums.originFills, Pass::OriginFill, 0.0F, 0);
}

void CpuBackend::addFills(const DrawList& list, std::span<const OverlayVertex> fills, Pass pass, float biasSlope,
                          int biasUnits)
{
    const double width = static_cast<double>(m_width) * m_supersample;
    const double height = static_cast<double>(m_height) * m_supersample;
    const auto [scale, offset] = list.frame.wideDepth;
    const auto projected = [&](const Clip& clip) {
        return Projected{.x = fixed((clip[0] / clip[3] + 1.0) * 0.5 * width),
                         .y = fixed((1.0 - clip[1] / clip[3]) * 0.5 * height),
                         .z = static_cast<float>((scale * clip[2] / clip[3]) + offset),
                         .normal = {},
                         .position = {},
                         .tint = {},
                         .across = 0.0F,
                         .along = 0.0F,
                         .span = 0.0F,
                         .valid = true};
    };
    for (std::size_t i = 0; i + 2 < fills.size(); i += 3) {
        std::array<Clip, 4> polygon{};
        std::size_t count = 0;
        for (std::size_t k = 0; k < 3; ++k) {
            const Clip from = wideClip(list.frame, fills[i + k].position, {});
            const Clip to = wideClip(list.frame, fills[i + ((k + 1) % 3)].position, {});
            const double near = from[2] + from[3];
            const double next = to[2] + to[3];
            if (near >= 0.0) {
                polygon.at(count++) = from;
            }
            if ((near >= 0.0) != (next >= 0.0)) {
                polygon.at(count++) = lerp(from, to, near / (near - next));
            }
        }
        if (count < 3) {
            continue;
        }
        const auto first = static_cast<std::uint32_t>(m_vertices.size());
        for (std::size_t k = 0; k < count; ++k) {
            m_vertices.push_back(projected(polygon.at(k)));
        }
        for (std::uint32_t k = 1; k + 1 < count; ++k) {
            m_triangles.push_back({.a = first,
                                   .b = first + k,
                                   .c = first + k + 1,
                                   .color = fills[i].color,
                                   .linear = {},
                                   .pass = pass,
                                   .hovered = false,
                                   .selected = false,
                                   .weight = 0.0F,
                                   .cull = false,
                                   .flat = true,
                                   .biasSlope = biasSlope,
                                   .biasUnits = static_cast<float>(biasUnits),
                                   .alongX = false,
                                   .id = 0,
                                   .pattern = 0});
        }
    }
}

void CpuBackend::addDatumLine(const DrawList& list, const DatumLine& line, Pass pass)
{
    const double width = static_cast<double>(m_width) * m_supersample;
    const double height = static_cast<double>(m_height) * m_supersample;
    const auto [scale, offset] = list.frame.wideDepth;
    Clip start = wideClip(list.frame, line.start, {});
    Clip end = wideClip(list.frame, line.end, {});
    const double nearStart = start[2] + start[3];
    const double nearEnd = end[2] + end[3];
    if (nearStart < 0.0 && nearEnd < 0.0) {
        return;
    }
    if (nearStart < 0.0) {
        start = lerp(start, end, nearStart / (nearStart - nearEnd));
    } else if (nearEnd < 0.0) {
        end = lerp(end, start, nearEnd / (nearEnd - nearStart));
    }
    const double widthPixels = static_cast<double>(line.width) * list.pixelRatio * m_supersample;
    const auto first = static_cast<std::uint32_t>(m_vertices.size());
    for (int corner = 0; corner < kEdgeCorners; ++corner) {
        const std::optional<Clip> at = expandEdge(start, end, corner, widthPixels, width, height, Cap::Butt);
        if (!at.has_value() || (*at)[3] <= 0.0) {
            m_vertices.resize(first);
            return;
        }
        const Clip& c = *at;
        m_vertices.push_back({.x = fixed((c[0] / c[3] + 1.0) * 0.5 * width),
                              .y = fixed((1.0 - c[1] / c[3]) * 0.5 * height),
                              .z = static_cast<float>((scale * c[2] / c[3]) + offset),
                              .normal = {},
                              .position = {},
                              .tint = {},
                              .across = 0.0F,
                              .along = 0.0F,
                              .span = 0.0F,
                              .valid = true});
    }
    const double dx = std::abs((end[0] / end[3]) - (start[0] / start[3])) * width;
    const double dy = std::abs((end[1] / end[3]) - (start[1] / start[3])) * height;
    Triangle lower{.a = first,
                   .b = first + 1,
                   .c = first + 2,
                   .color = line.color,
                   .linear = {},
                   .pass = pass,
                   .hovered = false,
                   .selected = false,
                   .weight = 0.0F,
                   .cull = false,
                   .flat = true,
                   .biasSlope = pass == Pass::OriginLine ? 0.0F : kGridBiasSlope,
                   .biasUnits = pass == Pass::OriginLine ? 0.0F : static_cast<float>(kGridBiasUnits),
                   .alongX = dx > dy,
                   .id = 0,
                   .pattern = static_cast<std::uint16_t>(line.pattern)};
    Triangle upper = lower;
    upper.a = first + 1;
    upper.b = first + 3;
    upper.c = first + 2;
    m_triangles.push_back(lower);
    m_triangles.push_back(upper);
}

bool CpuBackend::stippledOff(const Triangle& triangle, int x, int y) const
{
    const double at = triangle.alongX ? x + 0.5 : m_height - y - 0.5;
    const auto bit = static_cast<unsigned>(std::floor((at / m_datumDash) + 0.5)) & 15U;
    return (triangle.pattern & (1U << bit)) == 0U;
}

void CpuBackend::addGridLine(const DrawList& list, const OverlayLine& line,
                             const std::optional<std::array<double, 3>>& onPlane)
{
    const double width = static_cast<double>(m_width) * m_supersample;
    const double height = static_cast<double>(m_height) * m_supersample;
    const auto [scale, offset] = list.frame.wideDepth;
    Clip start = wideClip(list.frame, line.start, list.grid.shift);
    Clip end = wideClip(list.frame, line.end, list.grid.shift);
    const double nearStart = start[2] + start[3];
    const double nearEnd = end[2] + end[3];
    if (nearStart < 0.0 && nearEnd < 0.0) {
        return;
    }
    if (nearStart < 0.0) {
        start = lerp(start, end, nearStart / (nearStart - nearEnd));
    } else if (nearEnd < 0.0) {
        end = lerp(end, start, nearEnd / (nearEnd - nearStart));
    }
    const double widthPixels = static_cast<double>(line.width) * list.pixelRatio * m_supersample;
    const auto first = static_cast<std::uint32_t>(m_vertices.size());
    const Linear startTint = linearOf(line.startColor);
    const Linear endTint = linearOf(line.endColor);
    for (int corner = 0; corner < kEdgeCorners; ++corner) {
        const std::optional<Clip> at = expandEdge(start, end, corner, widthPixels, width, height);
        if (!at.has_value() || (*at)[3] <= 0.0) {
            m_vertices.resize(first);
            return;
        }
        const Clip& c = *at;
        const double x = c[0] / c[3];
        const double y = c[1] / c[3];
        const double z =
            onPlane ? ((*onPlane)[0] * x) + ((*onPlane)[1] * y) + (*onPlane)[2] : (scale * c[2] / c[3]) + offset;
        m_vertices.push_back({.x = fixed((x + 1.0) * 0.5 * width),
                              .y = fixed((1.0 - y) * 0.5 * height),
                              .z = static_cast<float>(z),
                              .normal = {},
                              .position = {},
                              .tint = corner >= 2 ? endTint : startTint,
                              .across = 0.0F,
                              .along = 0.0F,
                              .span = 0.0F,
                              .valid = true});
    }
    Triangle lower{.a = first,
                   .b = first + 1,
                   .c = first + 2,
                   .color = line.startColor,
                   .linear = {},
                   .pass = Pass::GridLine,
                   .hovered = false,
                   .selected = false,
                   .weight = 0.0F,
                   .cull = false,
                   .flat = false,
                   .biasSlope = kGridBiasSlope,
                   .biasUnits = static_cast<float>(kGridBiasUnits),
                   .alongX = false,
                   .id = 0,
                   .pattern = 0};
    Triangle upper = lower;
    upper.a = first + 1;
    upper.b = first + 3;
    upper.c = first + 2;
    m_triangles.push_back(lower);
    m_triangles.push_back(upper);
}

} // namespace kalve137::render
