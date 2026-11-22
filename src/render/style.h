// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/color.h"

namespace kalve137::render {

enum class VisualStyle { Shaded, ShadedWithEdges, Wireframe, VisibleEdges };

inline constexpr double kBodyEdgeWidth = 1.5;
inline constexpr double kWireWidth = 1.0;

inline constexpr float kFaceBiasSlope = 1.0F;
inline constexpr int kFaceBiasUnits = 1;

inline constexpr float kHoverWeight = 0.6F;

struct ViewportStyle {
    snapshot::Color background;
    snapshot::Color body{0.64F, 0.68F, 0.73F};
    snapshot::Color bodyEdge{0.07F, 0.10F, 0.14F};
    snapshot::Color wire{0.77F, 0.79F, 0.81F};
    snapshot::Color hover{0.55F, 0.77F, 0.95F};
    snapshot::Color selected{0.96F, 0.77F, 0.22F};
    VisualStyle visualStyle = VisualStyle::ShadedWithEdges;
    double pixelRatio = 1.0;
    bool cullClosed = true;

    bool operator==(const ViewportStyle&) const = default;
};

} // namespace kalve137::render
