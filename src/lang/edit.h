// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <string>
#include <utility>

namespace kalve137::lang {

std::string setParam(const std::string& source, const std::string& name, const std::string& valueText);
std::string appendStatement(const std::string& source, const std::string& statementText);

std::string addHole(const std::string& source, const std::string& face, double u, double v, double diameter,
                    double depth);

std::string addConstraint(const std::string& source, const std::string& sketch, const std::string& constraintText);

using SketchPositions = std::map<std::string, std::map<std::string, std::pair<double, double>>>;

std::string settleGuesses(const std::string& source, const SketchPositions& solved);

} // namespace kalve137::lang
