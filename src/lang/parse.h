// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lang/ast.h"

#include <string>
#include <vector>

namespace kalve137::lang {

struct ParseResult {
    Document document;
    std::vector<Diagnostic> diagnostics;
};

ParseResult parse(const std::string& source);

struct ExpressionResult {
    ExprPtr expr;
    std::vector<Diagnostic> diagnostics;
};

ExpressionResult parseExpression(const std::string& text);

bool isUnit(const std::string& word);
bool isThreadName(const std::string& word);
bool isKeyword(const std::string& word);
std::vector<std::string> keywords();
std::vector<std::string> units();

} // namespace kalve137::lang
