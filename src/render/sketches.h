// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/draw_list.h"
#include "snapshot/color.h"
#include "snapshot/sketch.h"

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace kalve137::render {

enum class SketchItem : std::uint8_t { Line, Point, Profile };

struct SketchKey {
    SketchItem item = SketchItem::Line;
    int feature = 0;
    int index = 0;

    auto operator<=>(const SketchKey&) const = default;
};

struct SketchLook {
    snapshot::Color free;
    snapshot::Color construction;
    snapshot::Color profile;
    snapshot::Color highlight;
    snapshot::Color hover;
    int editing = 0;
    bool lines = false;
    bool points = false;
    bool profiles = false;
    std::set<std::pair<int, std::size_t>> highlighted;

    bool operator==(const SketchLook&) const = default;
};

struct SketchMarks {
    std::optional<SketchKey> hovered;
    std::set<SketchKey> selected;

    bool operator==(const SketchMarks&) const = default;
};

struct SketchGeometry {
    std::vector<DatumLine> curves;
    std::vector<OverlayVertex> profiles;
    std::vector<DatumLine> idCurves;
    std::vector<OverlayVertex> idProfiles;
    std::vector<MarkerItem> markers;
    std::vector<SketchKey> keys;
    std::vector<std::uint32_t> exactIds;
    bool capped = false;

    bool drawsLike(const SketchGeometry& other) const
    {
        return curves == other.curves && profiles == other.profiles && idCurves == other.idCurves &&
               idProfiles == other.idProfiles;
    }

    bool picksLike(const SketchGeometry& other) const
    {
        const auto picked = [](const MarkerItem& a, const MarkerItem& b) {
            return a.anchor == b.anchor && a.shape == b.shape && a.scale == b.scale && a.flags == b.flags &&
                   a.pick == b.pick;
        };
        return idCurves == other.idCurves && idProfiles == other.idProfiles && keys == other.keys &&
               exactIds == other.exactIds && std::ranges::equal(markers, other.markers, picked);
    }
};

struct SketchStroke {
    snapshot::Polyline points;
    snapshot::Color color;
    double width = 0.0;
    std::uint32_t pattern = kSolidPattern;

    bool operator==(const SketchStroke&) const = default;
};

struct SketchTriangle {
    std::array<snapshot::Point, 3> corners{};
    snapshot::Color color;

    bool operator==(const SketchTriangle&) const = default;
};

struct ActiveSketch {
    snapshot::SketchProfiles profiles;
    snapshot::Color profileColor;
    std::vector<SketchStroke> strokes;
    std::vector<SketchTriangle> arrows;

    bool operator==(const ActiveSketch&) const = default;
};

struct SketchPreview {
    std::vector<SketchStroke> strokes;
    std::vector<MarkerItem> markers;

    bool operator==(const SketchPreview&) const = default;
};

struct ActiveGeometry {
    std::vector<OverlayVertex> profiles;
    std::vector<DatumLine> lines;
    std::vector<OverlayVertex> arrows;

    bool operator==(const ActiveGeometry&) const = default;
};

ActiveGeometry placeActiveSketch(const ActiveSketch& sketch, const snapshot::Point& origin);
std::vector<DatumLine> placeStrokes(std::span<const SketchStroke> strokes, const snapshot::Point& origin);

SketchGeometry buildSketches(std::span<const snapshot::Sketch> sketches, const SketchLook& look,
                             const SketchMarks& marks, const snapshot::Point& origin);
std::optional<SketchKey> sketchAt(const SketchGeometry& geometry, std::uint32_t entry);
std::optional<snapshot::Point> sketchPointOf(std::span<const snapshot::Sketch> sketches, const SketchKey& key);

} // namespace kalve137::render
