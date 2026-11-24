// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-FileCopyrightText: 1999-2014 OPEN CASCADE SAS
// SPDX-License-Identifier: GPL-3.0-or-later AND LGPL-2.1-only WITH OCCT-exception-1.0

#include "render/datums.h"

#include "render/pick_id.h"
#include "render/positions.h"
#include "render/style.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace kalve137::render {

namespace {

constexpr float kByte = 255.0F;
constexpr int kQuadCorners = 4;

struct Square {
    std::array<camera::Vec3, kQuadCorners> corners;
    double depth = 0.0;
    std::uint32_t entry = 0;
    DatumKey key;
};

using Offsets = std::array<std::array<double, 2>, kQuadCorners>;

class Builder {
public:
    Builder(const Datums& datums, const camera::Camera& camera, const camera::Viewport& viewport,
            const snapshot::Point& origin, const DatumMarks& marks, DatumGeometry& geometry)
        : m_camera(camera), m_viewport(viewport), m_origin(origin), m_marks(marks), m_geometry(geometry),
          m_planeColor(datums.planeColor)
    {
        m_camera.setAspect(viewport.aspect());
    }

    std::vector<Square> squares(std::span<const DatumPlane> planes, DatumKind kind, std::uint32_t first,
                                const Offsets& offsets)
    {
        std::vector<Square> result;
        result.reserve(planes.size());
        for (std::size_t index = 0; index < planes.size(); ++index) {
            const DatumPlane& plane = planes[index];
            const double scale = anchorScale(m_camera, m_viewport, plane.anchor);
            Square square;
            square.entry = first + static_cast<std::uint32_t>(index);
            square.key = {.kind = kind, .id = plane.id};
            camera::Vec3 sum;
            for (std::size_t i = 0; i < square.corners.size(); ++i) {
                const camera::Vec3 corner =
                    plane.anchor + (plane.u * (offsets.at(i)[0] * scale)) + (plane.v * (offsets.at(i)[1] * scale));
                square.corners.at(i) = corner;
                sum = sum + corner;
                include(corner);
            }
            square.depth = camera::dot((sum / kQuadCorners) - m_camera.eye(), m_camera.direction());
            result.push_back(square);
        }
        std::ranges::stable_sort(result, std::ranges::greater{}, &Square::depth);
        return result;
    }

    void addSquares(const std::vector<Square>& squares, std::vector<OverlayVertex>& fills,
                    std::vector<DatumLine>& lines, std::vector<OverlayVertex>& idFills, std::vector<DatumLine>& idLines)
    {
        for (const Square& square : squares) {
            const VertexColor fill = colorOf(m_planeColor, square.key, kDatumPlaneAlpha);
            for (const std::size_t i : {0U, 1U, 2U, 0U, 2U, 3U}) {
                fills.push_back({.position = place(square.corners.at(i)), .color = fill});
                if (pickable(square.entry)) {
                    idFills.push_back(
                        {.position = place(square.corners.at(i)), .color = idColor(square.entry, PickKind::Face)});
                }
            }
        }
        for (const Square& square : squares) {
            const VertexColor outline = colorOf(m_planeColor, square.key, 1.0F);
            for (std::size_t i = 0; i < square.corners.size(); ++i) {
                const camera::Vec3& start = square.corners.at(i);
                const camera::Vec3& end = square.corners.at((i + 1) % square.corners.size());
                addLine(lines, start, end, outline, kDatumOutlineWidth, kSolidPattern);
                if (pickable(square.entry)) {
                    addLine(idLines, start, end, idColor(square.entry, PickKind::Point),
                            std::max(kDatumOutlineWidth, kPickEdgeWidth), kSolidPattern);
                }
            }
        }
    }

    void addAxes(std::span<const DatumAxis> axes, DatumKind kind, std::uint32_t first, double from, double to,
                 std::uint32_t pattern, std::vector<DatumLine>& lines, std::vector<DatumLine>& idLines)
    {
        for (std::size_t index = 0; index < axes.size(); ++index) {
            const DatumAxis& axis = axes[index];
            const double scale = anchorScale(m_camera, m_viewport, axis.anchor);
            const camera::Vec3 start = axis.anchor + (axis.direction * (from * scale));
            const camera::Vec3 end = axis.anchor + (axis.direction * (to * scale));
            include(start);
            include(end);
            addLine(lines, start, end, colorOf(axis.color, {.kind = kind, .id = axis.id}, 1.0F), kDatumAxisWidth,
                    pattern);
            const std::uint32_t entry = first + static_cast<std::uint32_t>(index);
            if (pickable(entry)) {
                addLine(idLines, start, end, idColor(entry, PickKind::Edge), std::max(kDatumAxisWidth, kPickEdgeWidth),
                        kSolidPattern);
            }
        }
    }

    void addPoints(const Datums& datums)
    {
        if (datums.originPoint) {
            include({});
            m_geometry.markers.push_back({.anchor = {},
                                          .shape = MarkerShape::Ball,
                                          .scale = MarkerScale::Medium,
                                          .color = datums.originPointColor,
                                          .flags = kMarkerTopmost,
                                          .pick = 0});
        }
        for (std::size_t index = 0; index < datums.points.size(); ++index) {
            const DatumPoint& point = datums.points[index];
            include(point.anchor);
            const DatumKey key{.kind = DatumKind::Point, .id = point.id};
            const Encoded shown =
                highlighted(encodedOf(m_planeColor), m_marks.hovered == key, m_marks.selected.contains(key));
            const auto entry = static_cast<std::uint32_t>(index);
            const bool picks = pickable(entry);
            m_geometry.markers.push_back(
                {.anchor = point.anchor,
                 .shape = MarkerShape::RingPlus,
                 .scale = MarkerScale::Medium,
                 .color = {.red = shown[0], .green = shown[1], .blue = shown[2], .alpha = 1.0F},
                 .flags = static_cast<std::uint8_t>(kMarkerTopmost | (picks ? kMarkerPickable : 0)),
                 .pick = picks ? encodePickId(kMarkerChunk, PickKind::Point, entry) : 0});
        }
    }

private:
    bool pickable(std::uint32_t entry)
    {
        if (entry < kDatumEntries) {
            return true;
        }
        m_geometry.capped = true;
        return false;
    }

    Position place(const camera::Vec3& point) const { return relativePosition({point.x, point.y, point.z}, m_origin); }

    VertexColor colorOf(const snapshot::Color& base, const DatumKey& key, float alpha) const
    {
        const Encoded shown = highlighted(encodedOf(base), m_marks.hovered == key, m_marks.selected.contains(key));
        VertexColor color = vertexColor({.red = shown[0], .green = shown[1], .blue = shown[2], .alpha = 1.0F});
        color[3] = static_cast<std::uint8_t>(std::lround(alpha * kByte));
        return color;
    }

    void addLine(std::vector<DatumLine>& lines, const camera::Vec3& start, const camera::Vec3& end,
                 const VertexColor& color, double width, std::uint32_t pattern) const
    {
        lines.push_back({.start = place(start),
                         .end = place(end),
                         .color = color,
                         .width = static_cast<float>(width),
                         .pattern = static_cast<float>(pattern)});
    }

    void include(const camera::Vec3& point) { m_geometry.bounds = unite(m_geometry.bounds, camera::Box{point, point}); }

    camera::Camera m_camera;
    camera::Viewport m_viewport;
    snapshot::Point m_origin;
    const DatumMarks& m_marks;
    DatumGeometry& m_geometry;
    snapshot::Color m_planeColor;
};

template <typename Item>
bool holds(const std::vector<Item>& items, std::uint64_t id)
{
    return std::ranges::any_of(items, [id](const Item& item) { return item.id == id; });
}

} // namespace

double anchorScale(const camera::Camera& camera, const camera::Viewport& viewport, const camera::Vec3& anchor)
{
    if (!viewport.isValid()) {
        return 0.0;
    }
    camera::Camera shown = camera;
    shown.setAspect(viewport.aspect());
    const double depth = std::abs(camera::dot(anchor - shown.eye(), shown.direction()));
    return std::abs(shown.viewDimensions(depth).y) / viewport.height * viewport.pixelRatio;
}

DatumGeometry buildDatums(const Datums& datums, const camera::Camera& camera, const camera::Viewport& viewport,
                          const snapshot::Point& origin, const DatumMarks& marks)
{
    DatumGeometry geometry;
    if (!viewport.isValid() || datums.empty()) {
        return geometry;
    }
    Builder builder(datums, camera, viewport, origin, marks, geometry);
    const auto originAxesAt = static_cast<std::uint32_t>(datums.originPlanes.size());
    const auto planesAt = originAxesAt + static_cast<std::uint32_t>(datums.originAxes.size());
    const auto axesAt = planesAt + static_cast<std::uint32_t>(datums.planes.size());
    const double half = kConstructionPlanePixels * 0.5;
    builder.addSquares(builder.squares(datums.planes, DatumKind::Plane, planesAt,
                                       {{{-half, -half}, {half, -half}, {half, half}, {-half, half}}}),
                       geometry.fills, geometry.lines, geometry.idFills, geometry.idOutlines);
    const double reach = kConstructionAxisPixels * 0.5;
    builder.addAxes(datums.axes, DatumKind::Axis, axesAt, -reach, reach, kDashPattern, geometry.lines,
                    geometry.idLines);
    const double size = kOriginPlanePixels;
    builder.addSquares(builder.squares(datums.originPlanes, DatumKind::OriginPlane, 0,
                                       {{{0.0, 0.0}, {size, 0.0}, {size, size}, {0.0, size}}}),
                       geometry.originFills, geometry.originLines, geometry.idOriginFills, geometry.idOriginOutlines);
    builder.addAxes(datums.originAxes, DatumKind::OriginAxis, originAxesAt, 0.0, kOriginAxisPixels, kDotDashPattern,
                    geometry.originLines, geometry.idOriginLines);
    builder.addPoints(datums);
    return geometry;
}

std::optional<DatumKey> datumAt(const Datums& datums, std::uint32_t chunk, std::uint32_t entry)
{
    if (chunk == kMarkerChunk) {
        if (entry < datums.points.size()) {
            return DatumKey{.kind = DatumKind::Point, .id = datums.points[entry].id};
        }
        return std::nullopt;
    }
    std::size_t index = entry;
    if (index < datums.originPlanes.size()) {
        return DatumKey{.kind = DatumKind::OriginPlane, .id = datums.originPlanes[index].id};
    }
    index -= datums.originPlanes.size();
    if (index < datums.originAxes.size()) {
        return DatumKey{.kind = DatumKind::OriginAxis, .id = datums.originAxes[index].id};
    }
    index -= datums.originAxes.size();
    if (index < datums.planes.size()) {
        return DatumKey{.kind = DatumKind::Plane, .id = datums.planes[index].id};
    }
    index -= datums.planes.size();
    if (index < datums.axes.size()) {
        return DatumKey{.kind = DatumKind::Axis, .id = datums.axes[index].id};
    }
    return std::nullopt;
}

bool contains(const Datums& datums, const DatumKey& key)
{
    switch (key.kind) {
    case DatumKind::OriginPlane:
        return holds(datums.originPlanes, key.id);
    case DatumKind::OriginAxis:
        return holds(datums.originAxes, key.id);
    case DatumKind::Plane:
        return holds(datums.planes, key.id);
    case DatumKind::Axis:
        return holds(datums.axes, key.id);
    case DatumKind::Point:
        return holds(datums.points, key.id);
    }
    return false;
}

std::vector<camera::Vec3> datumAnchors(const Datums& datums)
{
    std::vector<camera::Vec3> anchors;
    anchors.reserve(datums.originPlanes.size() + datums.originAxes.size() + datums.planes.size() + datums.axes.size() +
                    datums.points.size() + 1);
    if (datums.originPoint) {
        anchors.emplace_back();
    }
    for (const DatumPlane& plane : datums.originPlanes) {
        anchors.push_back(plane.anchor);
    }
    for (const DatumAxis& axis : datums.originAxes) {
        anchors.push_back(axis.anchor);
    }
    for (const DatumPlane& plane : datums.planes) {
        anchors.push_back(plane.anchor);
    }
    for (const DatumAxis& axis : datums.axes) {
        anchors.push_back(axis.anchor);
    }
    for (const DatumPoint& point : datums.points) {
        anchors.push_back(point.anchor);
    }
    return anchors;
}

std::optional<camera::Box> fitBox(const std::optional<camera::Box>& bodies, std::span<const camera::Vec3> anchors,
                                  double emptyHalfSize)
{
    std::optional<camera::Box> around;
    for (const camera::Vec3& anchor : anchors) {
        around = unite(around, camera::Box{anchor, anchor});
    }
    if (bodies || !around) {
        return unite(bodies, around);
    }
    const camera::Vec3 center = around->center();
    const auto grown = [&](double low, double high, double middle) {
        return high - low >= 2.0 * emptyHalfSize ? std::array{low, high}
                                                 : std::array{middle - emptyHalfSize, middle + emptyHalfSize};
    };
    const std::array x = grown(around->min.x, around->max.x, center.x);
    const std::array y = grown(around->min.y, around->max.y, center.y);
    const std::array z = grown(around->min.z, around->max.z, center.z);
    return camera::Box{{x[0], y[0], z[0]}, {x[1], y[1], z[1]}};
}

} // namespace kalve137::render
