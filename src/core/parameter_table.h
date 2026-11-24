// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/document.h"
#include "core/parameters.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kalve137::core {

struct FieldRef {
    FeatureId feature = 0;
    std::string key;
    sketch::Id constraint = 0;
};

bool takesExpression(sketch::ConstraintKind kind);

std::optional<ExpressionError> applyParameters(Document& document);
void reconcileExpressions(const Document& before, Document& after);
std::optional<ParameterValues> parameterValues(const Document& document);

std::optional<ExpressionError> addParameter(Document& document, const std::string& name, std::string_view expression);
std::optional<ExpressionError> setParameterExpression(Document& document, std::string_view name,
                                                      std::string_view expression);
std::optional<ExpressionError> renameParameter(Document& document, std::string_view from, const std::string& to);
std::optional<ExpressionError> removeParameter(Document& document, std::string_view name);
std::optional<ExpressionError> setFieldExpression(Document& document, const FieldRef& field, std::string_view text);

std::vector<std::string> usersOf(const Document& document, std::string_view name);
std::optional<ExpressionError> checkNewParameterName(const Document& document, std::string_view name);
std::optional<ExpressionError> checkFeatureName(const Document& document, std::string_view name);

} // namespace kalve137::core
