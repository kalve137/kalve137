// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lang/ast.h"

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace kalve137::lang {

struct Dim {
    int length = 0;
    int angle = 0;

    bool operator==(const Dim&) const = default;
};

std::string toString(Dim dim);

struct Quantity {
    double value = 0.0;
    Dim dim;
    bool thread = false;
};

std::string toText(const Quantity& quantity);

using Params = std::map<std::string, Quantity>;

struct EvalError : std::runtime_error {
    EvalError(Span at, const std::string& message, std::string fix = {}, std::string kind = "build")
        : std::runtime_error(message), span(at), hint(std::move(fix)), code(std::move(kind))
    {}

    Span span;
    std::string hint;
    std::string code;
};

class Measurer {
public:
    virtual ~Measurer() = default;
    virtual Quantity measure(const Expr& call, const Params& params) = 0;
};

Quantity evaluate(const Expr& expr, const Params& params, Measurer* measurer = nullptr);

inline constexpr const char* kPieces = "pieces";

std::vector<Diagnostic> check(const Document& document);

bool isMeasurement(const std::string& function);
bool isFaceFunction(const std::string& function);
bool isPlaneName(const std::string& word);
bool hasLoop(const Statement& sketch);
std::vector<std::string> faceItems(const Statement& sketch);
std::vector<std::string> faceParts(const Document& document, const Statement& feature, const std::string& item);
std::vector<std::string> sideWords(const Document& document, const Statement& feature);
const Statement* sketchOf(const Document& document, const Statement& feature);

double clearanceDiameter(double thread);
double tapDiameter(double thread);

} // namespace kalve137::lang
