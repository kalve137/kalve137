// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lang/edit.h"

#include "lang/format.h"
#include "lang/parse.h"

#include <cmath>
#include <iterator>
#include <stdexcept>

namespace kalve137::lang {

namespace {

Document parsed(const std::string& source)
{
    ParseResult result = parse(source);
    if (hasErrors(result.diagnostics)) {
        throw std::runtime_error("The source does not parse: " + toText(result.diagnostics.front()));
    }
    return std::move(result.document);
}

} // namespace

std::string setParam(const std::string& source, const std::string& name, const std::string& valueText)
{
    Document document = parsed(source);
    ExpressionResult value = parseExpression(valueText);
    if (!value.expr) {
        throw std::runtime_error("'" + valueText + "' is not a value: " + value.diagnostics.front().message);
    }
    for (Statement& s : document.statements) {
        if (s.kind == Statement::Kind::Param && s.name == name) {
            s.value = value.expr;
            return format(document);
        }
    }
    throw std::runtime_error("There is no param named '" + name + "'");
}

std::string appendStatement(const std::string& source, const std::string& statementText)
{
    Document document = parsed(source);
    Document addition = parsed(statementText);
    for (Statement& s : addition.statements) {
        document.statements.push_back(std::move(s));
    }
    return format(document);
}

std::string addHole(const std::string& source, const std::string& face, double u, double v, double diameter,
                    double depth)
{
    const Document document = parsed(source);
    int n = 1;
    while (document.find("hole" + std::to_string(n)) != nullptr ||
           document.find("drill" + std::to_string(n)) != nullptr) {
        ++n;
    }
    const auto mm = [](double value) {
        const double rounded = std::round(value * 100.0) / 100.0;
        return formatNumber(rounded == 0.0 ? 0.0 : rounded) + " mm";
    };
    const std::string sketch = "hole" + std::to_string(n);
    Document addition = parsed("sketch " + sketch + " on " + face + " {\n  circle bore center (" + mm(u) + ", " +
                               mm(v) + ") diameter " + mm(diameter) + "\n}\n\ncut drill" + std::to_string(n) + " = " +
                               sketch + " depth " + mm(depth) + "\n");
    Document result = document;
    auto at = result.statements.end();
    while (at != result.statements.begin() && std::prev(at)->kind == Statement::Kind::Check) {
        --at;
    }
    result.statements.insert(at, addition.statements.begin(), addition.statements.end());
    return format(result);
}

std::string addConstraint(const std::string& source, const std::string& sketch, const std::string& constraintText)
{
    Document document = parsed(source);
    const Document addition = parsed("sketch s on XY {\n  " + constraintText + "\n}\n");
    const std::vector<SketchItem>& items = addition.statements.front().items;
    if (items.size() != 1 || items.front().kind != SketchItem::Kind::Constraint) {
        throw std::runtime_error("'" + constraintText + "' is not one constraint");
    }
    for (Statement& s : document.statements) {
        if (s.kind == Statement::Kind::Sketch && s.name == sketch) {
            s.items.push_back(items.front());
            return format(document);
        }
    }
    throw std::runtime_error("There is no sketch named '" + sketch + "'");
}

std::string settleGuesses(const std::string& source, const SketchPositions& solved)
{
    Document document = parsed(source);
    const auto length = [](double millimeters) {
        ExpressionResult result = parseExpression(formatLength(millimeters));
        return result.expr;
    };
    for (Statement& s : document.statements) {
        const auto sketch = solved.find(s.name);
        if (s.kind != Statement::Kind::Sketch || sketch == solved.end()) {
            continue;
        }
        for (SketchItem& item : s.items) {
            const auto point = sketch->second.find(item.name);
            if (item.kind == SketchItem::Kind::Point && item.center.x && point != sketch->second.end()) {
                item.center = {length(point->second.first), length(point->second.second)};
            }
        }
    }
    return format(document);
}

} // namespace kalve137::lang
