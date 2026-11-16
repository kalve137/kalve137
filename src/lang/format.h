// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lang/ast.h"

#include <string>

namespace kalve137::lang {

std::string format(const Document& document);
std::string format(const Expr& expr);
std::string format(const SketchItem& item);
std::string formatNumber(double value);
std::string formatLength(double millimeters);

} // namespace kalve137::lang
