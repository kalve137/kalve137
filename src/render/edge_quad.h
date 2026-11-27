// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>

namespace kalve137::render {

using Clip = std::array<double, 4>;

inline constexpr int kEdgeCorners = 4;

enum class Cap { Square, Butt };

std::optional<Clip> expandEdge(const Clip& start, const Clip& end, int corner, double widthPixels, double width,
                               double height, Cap cap = Cap::Square);

} // namespace kalve137::render
