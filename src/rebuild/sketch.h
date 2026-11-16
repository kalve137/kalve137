// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lang/check.h"
#include "model/snapshot.h"
#include "rebuild/outline.h"

#include <string>
#include <vector>

namespace kalve137::rebuild {

double lengthOf(const lang::Expr& expr, const lang::Params& params, const std::string& what, bool positive);

std::vector<Outline> sketchOutlines(const lang::Statement& sketch, const lang::Params& params,
                                    std::vector<lang::Diagnostic>& warnings, std::vector<model::SolvedSketch>& solved);

} // namespace kalve137::rebuild
