// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string_view>

namespace kalve137::core {

enum class Quantity { Length, Angle, Count };

std::optional<double> evaluate(std::string_view text, Quantity quantity);

} // namespace kalve137::core
