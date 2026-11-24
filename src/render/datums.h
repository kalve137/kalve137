// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "render/draw_list.h"
#include "snapshot/color.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace kalve137::render {

struct DatumPlane {
    std::uint64_t id = 0;
    camera::Vec3 anchor;
    camera::Vec3 u{1.0, 0.0, 0.0};
    camera::Vec3 v{0.0, 1.0, 0.0};

    bool operator==(const DatumPlane&) const = default;
};

struct DatumAxis {
    std::uint64_t id = 0;
    camera::Vec3 anchor;
    camera::Vec3 direction{0.0, 0.0, 1.0};
    snapshot::Color color;

    bool operator==(const DatumAxis&) const = default;
};

struct DatumPoint {
    std::uint64_t id = 0;
    camera::Vec3 anchor;

    bool operator==(const DatumPoint&) const = default;
};

struct Datums {
    std::vector<DatumPlane> originPlanes;
    std::vector<DatumAxis> originAxes;
    std::vector<DatumPlane> planes;
    std::vector<DatumAxis> axes;
    snapshot::Color planeColor;
    std::vector<DatumPoint> points;
    bool originPoint = false;
    snapshot::Color originPointColor;

    bool empty() const
    {
        return originPlanes.empty() && originAxes.empty() && planes.empty() && axes.empty() && points.empty() &&
               !originPoint;
    }
    bool operator==(const Datums&) const = default;
};

enum class DatumKind : std::uint8_t { OriginPlane, OriginAxis, Plane, Axis, Point };

struct DatumKey {
    DatumKind kind = DatumKind::OriginPlane;
    std::uint64_t id = 0;

    auto operator<=>(const DatumKey&) const = default;
};

struct DatumMarks {
    std::optional<DatumKey> hovered;
    std::set<DatumKey> selected;

    bool operator==(const DatumMarks&) const = default;
};

struct DatumGeometry {
    std::vector<OverlayVertex> fills;
    std::vector<DatumLine> lines;
    std::vector<OverlayVertex> originFills;
    std::vector<DatumLine> originLines;
    std::vector<OverlayVertex> idFills;
    std::vector<DatumLine> idOutlines;
    std::vector<DatumLine> idLines;
    std::vector<OverlayVertex> idOriginFills;
    std::vector<DatumLine> idOriginOutlines;
    std::vector<DatumLine> idOriginLines;
    std::vector<MarkerItem> markers;
    std::optional<camera::Box> bounds;
    bool capped = false;

    bool drawsLike(const DatumGeometry& other) const
    {
        return fills == other.fills && lines == other.lines && originFills == other.originFills &&
               originLines == other.originLines && idFills == other.idFills && idOutlines == other.idOutlines &&
               idLines == other.idLines && idOriginFills == other.idOriginFills &&
               idOriginOutlines == other.idOriginOutlines && idOriginLines == other.idOriginLines;
    }
};

inline constexpr std::uint32_t kDatumEntries = 1U << 16;

double anchorScale(const camera::Camera& camera, const camera::Viewport& viewport, const camera::Vec3& anchor);
DatumGeometry buildDatums(const Datums& datums, const camera::Camera& camera, const camera::Viewport& viewport,
                          const snapshot::Point& origin, const DatumMarks& marks = {});
std::optional<DatumKey> datumAt(const Datums& datums, std::uint32_t chunk, std::uint32_t entry);
bool contains(const Datums& datums, const DatumKey& key);
std::vector<camera::Vec3> datumAnchors(const Datums& datums);
std::optional<camera::Box> fitBox(const std::optional<camera::Box>& bodies, std::span<const camera::Vec3> anchors,
                                  double emptyHalfSize);

} // namespace kalve137::render
