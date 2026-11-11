// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QMargins>

class QApplication;

namespace kalve137::app::theme {

inline const QColor kSlateDeep{0x12, 0x1A, 0x24};
inline const QColor kSlate{0x1E, 0x2A, 0x3A};
inline const QColor kSlateLight{0x2B, 0x3A, 0x4E};
inline const QColor kSlateEdge{0x3A, 0x4B, 0x62};
inline const QColor kTitanium{0x8A, 0x90, 0x99};
inline const QColor kTitaniumLight{0xC5, 0xC9, 0xCF};
inline const QColor kTitaniumDim{0x6E, 0x76, 0x82};
inline const QColor kGold{0xF2, 0xB7, 0x05};

inline const QColor kSlateDisabled{0x18, 0x21, 0x2D};
inline const QColor kSlateEdgeDisabled{0x22, 0x2E, 0x3D};
inline const QColor kSlatePressed{0x24, 0x32, 0x4A};
inline const QColor kSlateHover{0x26, 0x34, 0x4A};
inline const QColor kSlateLightHover{0x34, 0x46, 0x5E};
inline const QColor kSlateEdgeHover{0x4A, 0x5D, 0x78};
inline const QColor kTitaniumBright{0xED, 0xEF, 0xF2};
inline const QColor kGoldLight{0xF5, 0xC5, 0x37};
inline const QColor kScrim{0x0B, 0x10, 0x16, 0x8C};

inline const QColor kSketchFree{0x6C, 0xB4, 0xEE};
inline const QColor kSketchConstrained = kTitaniumBright;
inline const QColor kSketchConstruction{0xE8, 0x95, 0x5B};
inline const QColor kConflict{0xE5, 0x48, 0x4D};
inline const QColor kConflictText{0xF7, 0x8B, 0x8D};
inline const QColor kWarning{0xE8, 0xA3, 0x3D};
inline const QColor kPlane{0x6C, 0x8E, 0xBF};
inline const QColor kAxisX{0xD9, 0x4F, 0x4F};
inline const QColor kAxisY{0x5F, 0xB8, 0x5F};
inline const QColor kAxisZ{0x4F, 0x86, 0xD9};

inline const QColor kCanvas{0x23, 0x2D, 0x3B};
inline const QColor kGridMinor{0x2E, 0x3A, 0x4A};
inline const QColor kGridMajor{0x44, 0x53, 0x67};
inline const QColor kGridLabel{0x8A, 0x96, 0xA6};
inline const QColor kOriginPlane{0xE8, 0xA8, 0x7C};
inline const QColor kOriginPoint{0xF4, 0xF5, 0xF7};
inline const QColor kHover{0x8C, 0xC4, 0xF2};
inline const QColor kHandle{0x2F, 0x8C, 0xEB};
inline const QColor kHandleOutline{0xF4, 0xF5, 0xF7};

inline constexpr double kTextBody = 9.0;
inline constexpr double kTextTitle = 11.0;

inline constexpr int kSpaceXs = 2;
inline constexpr int kSpaceS = 4;
inline constexpr int kSpaceM = 8;
inline constexpr int kSpaceL = 12;
inline constexpr int kSpaceXl = 16;
inline constexpr int kSpaceXxl = 24;

inline constexpr int kRadiusSmall = 2;
inline constexpr int kRadiusControl = 4;
inline constexpr int kRadiusPanel = 6;

struct Density {
    int controlHeight;
    int rowHeight;
    int panelMargin;
};
inline constexpr Density kDensityCompact{24, 24, kSpaceM};
inline constexpr Density kDensityRegular{28, 28, kSpaceL};
inline constexpr Density kDensityRoomy{32, 32, kSpaceXl};
inline constexpr Density kDensity = kDensityRegular;
inline constexpr QMargins kPanelMargins{kDensity.panelMargin, kDensity.panelMargin, kDensity.panelMargin,
                                        kDensity.panelMargin};
inline constexpr int kRowIconSize = 16;

inline constexpr int kWidthNarrow = 320;
inline constexpr int kWidthRegular = 480;
inline constexpr int kWidthWide = 640;

void apply(QApplication& app);

} // namespace kalve137::app::theme
