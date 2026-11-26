// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/color.h"

#include <cstdint>

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

inline constexpr double kGridLineWidth = 1.0;
inline constexpr double kGridAxisWidth = 1.5;
inline constexpr double kGridLineStrength = 1.0;
inline constexpr float kGridWashAlpha = 0.45F;
inline constexpr float kGridBiasSlope = kFaceBiasSlope + 1.0F;
inline constexpr int kGridBiasUnits = kFaceBiasUnits + 1;
inline constexpr float kGridWashBiasSlope = 2.0F;
inline constexpr int kGridWashBiasUnits = 4;
inline constexpr int kGridWashSegments = 96;

inline constexpr double kOriginPlanePixels = 80.0;
inline constexpr double kOriginAxisPixels = 104.0;
inline constexpr double kConstructionPlanePixels = 120.0;
inline constexpr double kConstructionAxisPixels = 160.0;
inline constexpr float kDatumPlaneAlpha = 0.45F;
inline constexpr double kDatumOutlineWidth = 1.0;
inline constexpr double kDatumAxisWidth = 2.0;
inline constexpr double kDatumDashPixels = 1.0;
inline constexpr double kSketchCurveWidth = 1.5;
inline constexpr double kSketchConstructionWidth = 1.0;
inline constexpr double kMarkerSmallRing = 9.0;
inline constexpr double kMarkerMediumRing = 11.0;
inline constexpr double kMarkerLargeRing = 17.0;
inline constexpr double kMarkerSmallCross = 13.0;
inline constexpr double kMarkerMediumCross = 15.0;
inline constexpr double kMarkerLargeCross = 21.0;
inline constexpr double kMarkerSmallDot = 2.0;
inline constexpr double kMarkerMediumDot = 3.0;
inline constexpr double kMarkerLargeDot = 4.0;
inline constexpr double kConstraintBadgePixels = 20.0;
inline constexpr double kBadgeCornerShare = 0.22;
inline constexpr double kBadgeIconShare = 0.95;
inline constexpr double kLabelTextPixels = 20.0;
inline constexpr double kLabelPadX = 4.0;
inline constexpr double kLabelPadY = 2.0;
inline constexpr double kLabelCorner = 4.0;
inline constexpr double kAnnotationStroke = 1.0;
inline constexpr double kHandleShaftPixels = 22.0;
inline constexpr double kHandleShaftWidth = 7.0;
inline constexpr double kHandleHeadPixels = 15.0;
inline constexpr double kHandleHeadWidth = 18.0;
inline constexpr double kHandleOutlineWidth = 1.5;
inline constexpr double kHandleGlowWidth = 7.0;
inline constexpr int kHandleGlowAlpha = 90;
inline constexpr int kHandleLitPercent = 125;
inline constexpr double kHandleGuideWidth = 1.5;
inline constexpr int kHandleGuideAlpha = 170;
inline constexpr std::uint32_t kSolidPattern = 0xFFFFU;
inline constexpr std::uint32_t kDashPattern = 0xFFC0U;
inline constexpr std::uint32_t kDotDashPattern = 0xFF18U;
inline constexpr std::uint32_t kDotPattern = 0xCCCCU;

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
