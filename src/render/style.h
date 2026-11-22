// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/color.h"

namespace kalve137::render {

enum class VisualStyle { Shaded, ShadedWithEdges, Wireframe, VisibleEdges };

inline constexpr double kBodyEdgeWidth = 1.5;
inline constexpr double kWireWidth = 1.0;
inline constexpr double kHighlightEdgeWidth = 2.0;

inline constexpr float kFaceBiasSlope = 1.0F;
inline constexpr int kFaceBiasUnits = 1;

inline constexpr float kHoverStep = 0.3F;
inline constexpr float kNearWhite = 0.8F;
inline constexpr snapshot::Color kSelectedGold{1.0F, 0.70F, 0.0F};
inline constexpr float kSelectedMix = 0.5F;
inline constexpr double kHaloWidth = 3.0;
inline constexpr float kHaloStep = 0.35F;
inline constexpr float kHiddenFaceWeight = 0.3F;
inline constexpr double kHiddenDash = 6.0;
inline constexpr double kHiddenGap = 4.0;
inline constexpr float kWireFaceWeight = 0.15F;

struct ViewportStyle {
    snapshot::Color background;
    snapshot::Color body{0.64F, 0.68F, 0.73F};
    snapshot::Color bodyEdge{0.07F, 0.10F, 0.14F};
    snapshot::Color wire{0.77F, 0.79F, 0.81F};
    VisualStyle visualStyle = VisualStyle::ShadedWithEdges;
    double pixelRatio = 1.0;
    bool cullClosed = true;

    bool operator==(const ViewportStyle&) const = default;
};

} // namespace kalve137::render
