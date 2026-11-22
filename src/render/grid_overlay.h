// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "render/draw_list.h"
#include "render/grid.h"
#include "snapshot/color.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace kalve137::render {

struct GridColors {
    snapshot::Color background;
    snapshot::Color minor;
    snapshot::Color major;
    snapshot::Color axisX;
    snapshot::Color axisY;
    snapshot::Color axisZ;

    bool operator==(const GridColors&) const = default;
};

struct GridSettings {
    bool visible = false;
    bool adaptive = true;
    double spacing = 10.0;
    GridPlane plane;
    std::optional<GridExtent> extent;
    GridColors colors;

    bool operator==(const GridSettings&) const = default;
};

class GridOverlay {
public:
    void setSettings(const GridSettings& settings);
    const GridSettings& settings() const { return m_settings; }
    const GridLayout& layout() const { return m_layout; }
    void update(const camera::Camera& camera, const camera::Viewport& viewport, const snapshot::Point& origin);
    GridDraw draw(const camera::Camera& camera, const camera::Viewport& viewport) const;
    std::optional<camera::Box> bounds() const;

private:
    void clear();
    void rebuild();

    GridSettings m_settings;
    GridLayout m_layout;
    snapshot::Point m_origin{};
    bool m_stale = true;
    std::uint64_t m_revision = 0;
    std::vector<OverlayVertex> m_wash;
    std::vector<OverlayLine> m_lines;
};

} // namespace kalve137::render
