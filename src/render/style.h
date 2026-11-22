// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/color.h"

namespace kalve137::render {

enum class VisualStyle { Shaded, ShadedWithEdges, Wireframe, VisibleEdges };

struct ViewportStyle {
    snapshot::Color background;
    snapshot::Color body{0.64F, 0.68F, 0.73F};
    VisualStyle visualStyle = VisualStyle::ShadedWithEdges;
    double pixelRatio = 1.0;
    bool cullClosed = true;

    bool operator==(const ViewportStyle&) const = default;
};

} // namespace kalve137::render
