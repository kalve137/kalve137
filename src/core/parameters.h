// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/expression.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kalve137::core {

struct Dim {
    int length = 0;
    int angle = 0;

    bool operator==(const Dim&) const = default;
};

inline constexpr Dim kPlainDim{0, 0};
inline constexpr Dim kLengthDim{1, 0};
inline constexpr Dim kAngleDim{0, 1};

struct ParameterValue {
    double value = 0.0;
    Dim dim;
    bool anyDim = false;
};

using ParameterValues = std::map<std::string, ParameterValue, std::less<>>;

struct Parameter {
    std::string name;
    std::string expression;
};

struct ExpressionError {
    std::string code;
    std::string message;
    std::string hint;
};

struct ExpressionScope {
    const ParameterValues* values = nullptr;
    std::vector<std::string> later;
};

struct ExpressionResult {
    double value = 0.0;
    Dim dim;
    bool anyDim = false;
    std::string canonical;
    std::vector<std::string> names;
    std::optional<ExpressionError> error;

    bool ok() const { return !error.has_value(); }
};

std::optional<ExpressionError> checkParameterName(std::string_view name);
bool namesParameter(std::string_view text);

ExpressionResult evaluateParameter(std::string_view text, const ExpressionScope& scope);
ExpressionResult evaluateField(std::string_view text, Quantity quantity, const ExpressionScope& scope);

std::string renameInExpression(std::string_view canonical, std::string_view from, std::string_view to);
std::string formatNumber(double value);
std::string formatParameterValue(const ParameterValue& value);
std::string describeDim(Dim dim);

} // namespace kalve137::core
