// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace kalve137::render {

struct Color {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 1.0F;

    bool operator==(const Color&) const = default;
};

struct ViewportStyle {
    Color background;
    double pixelRatio = 1.0;

    bool operator==(const ViewportStyle&) const = default;
};

} // namespace kalve137::render
