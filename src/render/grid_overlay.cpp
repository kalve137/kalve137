// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/grid_overlay.h"

#include "render/style.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kalve137::render {

namespace {

constexpr float kByte = 255.0F;

using LinearColor = std::array<double, 3>;

LinearColor linearColor(const snapshot::Color& color)
{
    return {toLinear(color.red), toLinear(color.green), toLinear(color.blue)};
}

std::uint8_t byteOf(double linear)
{
    return static_cast<std::uint8_t>(std::lround(toSrgb(static_cast<float>(linear)) * kByte));
}

VertexColor faded(const LinearColor& color, const LinearColor& background, double fade)
{
    const double weight = 1.0 - (kGridLineStrength * (1.0 - fade));
    const auto mixed = [&](std::size_t c) { return color.at(c) + ((background.at(c) - color.at(c)) * weight); };
    return {byteOf(mixed(0)), byteOf(mixed(1)), byteOf(mixed(2)), static_cast<std::uint8_t>(kByte)};
}

const snapshot::Color& axisColor(const GridColors& colors, const camera::Vec3& direction)
{
    const double x = std::abs(direction.x);
    const double y = std::abs(direction.y);
    const double z = std::abs(direction.z);
    if (x >= y && x >= z) {
        return colors.axisX;
    }
    return y >= z ? colors.axisY : colors.axisZ;
}

} // namespace

void GridOverlay::setSettings(const GridSettings& settings)
{
    if (settings == m_settings) {
        return;
    }
    m_settings = settings;
    m_stale = true;
}

void GridOverlay::update(const camera::Camera& camera, const camera::Viewport& viewport, const snapshot::Point& origin)
{
    if (!m_settings.visible || !viewport.isValid()) {
        clear();
        return;
    }
    camera::Camera shown = camera;
    shown.setAspect(viewport.aspect());
    const double perPixel = shown.worldPerPixel(viewport);
    const double step = gridStep(m_settings.adaptive, m_settings.spacing, perPixel * kGridMinCellPixels);
    const double logicalWidth = viewport.width / viewport.pixelRatio;
    const double logicalHeight = viewport.height / viewport.pixelRatio;
    const double viewRadius = 0.5 * std::hypot(logicalWidth, logicalHeight) * perPixel * kGridRadiusFactor;
    const GridLayout wanted = layoutGrid(m_settings.plane, step, shown.center(), viewRadius, m_settings.extent);
    if (!m_stale && origin == m_origin && !isGridStale(m_layout, wanted)) {
        return;
    }
    m_layout = wanted;
    m_origin = origin;
    m_stale = false;
    rebuild();
}

GridDraw GridOverlay::draw(const camera::Camera& camera, const camera::Viewport& viewport) const
{
    GridDraw grid{.revision = m_revision, .wash = m_wash, .lines = m_lines, .shift = {}};
    if (m_lines.empty() || !viewport.isValid()) {
        return grid;
    }
    camera::Camera shown = camera;
    shown.setAspect(viewport.aspect());
    const camera::Vec3& normal = m_settings.plane.normal;
    const double away = camera::dot(normal, shown.direction()) < 0.0 ? -1.0 : 1.0;
    const camera::Vec3 shift = normal * (away * kGridDepthPixels * shown.worldPerPixel(viewport));
    grid.shift = {static_cast<float>(shift.x), static_cast<float>(shift.y), static_cast<float>(shift.z)};
    const camera::Vec3 through = m_settings.plane.origin - camera::Vec3{m_origin[0], m_origin[1], m_origin[2]} + shift;
    grid.plane = {static_cast<float>(normal.x), static_cast<float>(normal.y), static_cast<float>(normal.z),
                  static_cast<float>(-camera::dot(normal, through))};
    return grid;
}

std::optional<camera::Box> GridOverlay::bounds() const
{
    if (m_lines.empty()) {
        return std::nullopt;
    }
    const camera::Vec3 center = gridPoint(m_settings.plane, m_layout.center.u, m_layout.center.v);
    const camera::Vec3 reach{m_layout.radius, m_layout.radius, m_layout.radius};
    return camera::Box{center - reach, center + reach};
}

void GridOverlay::clear()
{
    m_stale = true;
    m_layout = {};
    if (m_lines.empty() && m_wash.empty()) {
        return;
    }
    m_lines.clear();
    m_wash.clear();
    ++m_revision;
}

void GridOverlay::rebuild()
{
    ++m_revision;
    m_lines.clear();
    m_wash.clear();
    const GridPlane& plane = m_settings.plane;
    const GridColors& colors = m_settings.colors;
    const LinearColor background = linearColor(colors.background);
    const auto at = [&](double u, double v) {
        const camera::Vec3 point = gridPoint(plane, u, v);
        return relativePosition({point.x, point.y, point.z}, m_origin);
    };
    std::vector<GridLine> lines = gridLines(m_layout);
    std::ranges::stable_sort(lines, {}, &GridLine::kind);
    m_lines.reserve(lines.size() * static_cast<std::size_t>(kGridPiecesPerLine));
    for (const GridLine& line : lines) {
        const snapshot::Color shown = line.kind == GridLineKind::Minor ? colors.minor
                                      : line.kind == GridLineKind::Major
                                          ? colors.major
                                          : axisColor(colors, gridAxisDirection(plane, line));
        const LinearColor color = linearColor(shown);
        const auto width = static_cast<float>(line.kind == GridLineKind::Axis ? kGridAxisWidth : kGridLineWidth);
        const double piece = (line.end - line.start) / kGridPiecesPerLine;
        for (int p = 0; p < kGridPiecesPerLine; ++p) {
            const double from = line.start + (p * piece);
            const double to = line.start + ((p + 1) * piece);
            const double u0 = line.alongV ? line.offset : from;
            const double v0 = line.alongV ? from : line.offset;
            const double u1 = line.alongV ? line.offset : to;
            const double v1 = line.alongV ? to : line.offset;
            m_lines.push_back({.start = at(u0, v0),
                               .end = at(u1, v1),
                               .startColor = faded(color, background, gridFade(m_layout, u0, v0)),
                               .endColor = faded(color, background, gridFade(m_layout, u1, v1)),
                               .width = width});
        }
    }
    if (m_settings.extent || m_lines.empty()) {
        return;
    }
    VertexColor wash = vertexColor(colors.background);
    wash[3] = static_cast<std::uint8_t>(std::lround(kGridWashAlpha * kByte));
    const GridPoint& middle = m_layout.center;
    const double radius = m_layout.radius;
    m_wash.reserve(static_cast<std::size_t>(kGridWashSegments) * 3);
    for (int i = 0; i < kGridWashSegments; ++i) {
        const double a = 2.0 * std::numbers::pi * i / kGridWashSegments;
        const double b = 2.0 * std::numbers::pi * (i + 1) / kGridWashSegments;
        m_wash.push_back({.position = at(middle.u, middle.v), .color = wash});
        m_wash.push_back(
            {.position = at(middle.u + (radius * std::cos(a)), middle.v + (radius * std::sin(a))), .color = wash});
        m_wash.push_back(
            {.position = at(middle.u + (radius * std::cos(b)), middle.v + (radius * std::sin(b))), .color = wash});
    }
}

} // namespace kalve137::render
