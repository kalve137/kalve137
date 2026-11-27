// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/parameter_table.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <utility>
#include <variant>

namespace kalve137::core {

namespace {

constexpr double kMinSize = 0.001;
constexpr double kMaxLength = 100000.0;
constexpr double kMaxTaper = 89.0;
constexpr double kMaxSketchAngle = 180.0;
constexpr double kMinFullAngle = 0.1;
constexpr double kFullTurn = 360.0;
constexpr double kMinConeAngle = 1.0;
constexpr double kMaxConeAngle = 179.0;
constexpr std::size_t kMaxSetDigits = 6;
constexpr double kShownScale = 1e6;
constexpr double kSameNumber = 1e-9;

struct Slot {
    double* value = nullptr;
    int* count = nullptr;
    Quantity quantity = Quantity::Length;
    double minimum = kMinSize;
    double maximum = kMaxLength;
    bool keepSign = false;
    bool nonZero = false;
};

Slot length(double& value, double minimum = kMinSize)
{
    return {&value, nullptr, Quantity::Length, minimum, kMaxLength};
}

Slot angle(double& value, double minimum, double maximum)
{
    return {&value, nullptr, Quantity::Angle, minimum, maximum};
}

std::optional<std::size_t> setIndex(std::string_view key)
{
    constexpr std::string_view kPrefix = "sets.";
    constexpr std::string_view kSuffix = ".radius";
    if (!key.starts_with(kPrefix) || !key.ends_with(kSuffix) || key.size() <= kPrefix.size() + kSuffix.size()) {
        return std::nullopt;
    }
    const std::string_view digits = key.substr(kPrefix.size(), key.size() - kPrefix.size() - kSuffix.size());
    if (digits.size() > kMaxSetDigits || (digits.size() > 1 && digits.front() == '0') ||
        !std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::size_t index = 0;
    for (const char c : digits) {
        index = index * 10 + static_cast<std::size_t>(c - '0');
    }
    return index;
}

std::optional<Slot> slotOf(FeatureData& data, std::string_view key)
{
    return std::visit(
        Overloaded{
            [&](ExtrudeFeature& f) -> std::optional<Slot> {
                if (key == "distance") {
                    Slot slot = length(f.distance);
                    slot.keepSign = true;
                    return slot;
                }
                if (key == "second_distance") {
                    return length(f.secondDistance);
                }
                if (key == "taper") {
                    return angle(f.taperDegrees, -kMaxTaper, kMaxTaper);
                }
                return std::nullopt;
            },
            [&](RevolveFeature& f) -> std::optional<Slot> {
                return key == "angle" ? std::optional(angle(f.angleDegrees, kMinFullAngle, kFullTurn)) : std::nullopt;
            },
            [&](FilletFeature& f) -> std::optional<Slot> {
                const std::optional<std::size_t> index = setIndex(key);
                return index && *index < f.sets.size() ? std::optional(length(f.sets[*index].radius)) : std::nullopt;
            },
            [&](ChamferFeature& f) -> std::optional<Slot> {
                return key == "distance" ? std::optional(length(f.distance)) : std::nullopt;
            },
            [&](ShellFeature& f) -> std::optional<Slot> {
                return key == "thickness" ? std::optional(length(f.thickness)) : std::nullopt;
            },
            [&](OffsetFaceFeature& f) -> std::optional<Slot> {
                if (key != "distance") {
                    return std::nullopt;
                }
                Slot slot = length(f.distance, -kMaxLength);
                slot.nonZero = true;
                return slot;
            },
            [&](HoleFeature& f) -> std::optional<Slot> {
                if (key == "diameter") {
                    return length(f.diameter);
                }
                if (key == "depth") {
                    return length(f.depth);
                }
                if (key == "head_diameter") {
                    return length(f.headDiameter);
                }
                if (key == "head_depth") {
                    return length(f.headDepth);
                }
                if (key == "countersink_angle") {
                    return angle(f.countersinkAngle, kMinConeAngle, kMaxConeAngle);
                }
                if (key == "tip_angle") {
                    return angle(f.tipAngle, 0.0, kMaxConeAngle);
                }
                return std::nullopt;
            },
            [&](PlaneFeature& f) -> std::optional<Slot> {
                if (key == "offset" && f.kind == PlaneKind::Offset) {
                    return length(f.offset, -kMaxLength);
                }
                if (key == "angle" && f.kind == PlaneKind::Angle) {
                    return angle(f.angleDegrees, -kFullTurn, kFullTurn);
                }
                return std::nullopt;
            },
            [&](PatternFeature& f) -> std::optional<Slot> {
                if (key == "count") {
                    return Slot{nullptr, &f.count, Quantity::Count, kMinPatternCount, kMaxPatternCount};
                }
                if (key == "spacing") {
                    return length(f.spacing, -kMaxLength);
                }
                if (key == "angle") {
                    return angle(f.angleDegrees, kMinFullAngle, kFullTurn);
                }
                return std::nullopt;
            },
            [](auto&) -> std::optional<Slot> { return std::nullopt; },
        },
        data);
}

std::optional<Slot> slotOf(sketch::Constraint& constraint)
{
    if (!takesExpression(constraint.kind)) {
        return std::nullopt;
    }
    if (constraint.kind == sketch::ConstraintKind::Angle) {
        return angle(constraint.value, kMinSize, kMaxSketchAngle);
    }
    return length(constraint.value);
}

std::string fieldLabel(const Feature& feature, std::string_view key)
{
    if (const std::optional<std::size_t> index = setIndex(key)) {
        return feature.name + " set " + std::to_string(*index + 1) + " radius";
    }
    std::string text(key);
    std::ranges::replace(text, '_', ' ');
    return feature.name + " " + text;
}

std::string joined(const std::vector<std::string>& words)
{
    std::string text;
    for (std::size_t i = 0; i < words.size(); ++i) {
        text += (i == 0 ? "" : (i + 1 == words.size() ? " and " : ", ")) + words[i];
    }
    return text;
}

ExpressionError labelled(const std::string& label, ExpressionError error)
{
    error.message = label + ": " + error.message;
    return error;
}

std::string valueText(double exact, Quantity quantity)
{
    const double rounded = std::round(exact * kShownScale) / kShownScale;
    const double number = rounded == 0.0 ? 0.0 : rounded;
    switch (quantity) {
    case Quantity::Length:
        return formatNumber(number) + " mm";
    case Quantity::Angle:
        return formatNumber(number) + " deg";
    case Quantity::Count:
        return formatNumber(number);
    }
    return formatNumber(number);
}

std::string constraintLabel(const Feature& feature, const sketch::Constraint& constraint)
{
    std::string kind(sketch::toString(constraint.kind));
    std::ranges::replace(kind, '_', ' ');
    const Quantity quantity = constraint.kind == sketch::ConstraintKind::Angle ? Quantity::Angle : Quantity::Length;
    return feature.name + " " + kind + " " + valueText(constraint.value, quantity);
}

std::optional<ExpressionError> refusal(const Slot& slot, double number, const std::string& label)
{
    const std::string range = valueText(slot.minimum, slot.quantity) + " to " + valueText(slot.maximum, slot.quantity);
    if (slot.quantity == Quantity::Count && !(std::abs(number - std::round(number)) <= kSameNumber)) {
        return ExpressionError{"stamp", label + " takes a whole number, not " + formatNumber(number),
                               "Use a whole number from " + range};
    }
    if (!std::isfinite(number) || number < slot.minimum || number > slot.maximum ||
        (slot.nonZero && std::abs(number) < kMinSize)) {
        return ExpressionError{"stamp", label + " can't be " + valueText(number, slot.quantity),
                               slot.nonZero ? "Use a value from " + range + " other than 0"
                                            : "Use a value from " + range};
    }
    return std::nullopt;
}

void write(const Slot& slot, double number)
{
    if (slot.count != nullptr) {
        *slot.count = static_cast<int>(std::lround(number));
    } else if (slot.keepSign && *slot.value < 0.0) {
        *slot.value = -number;
    } else {
        *slot.value = number;
    }
}

double numberIn(const Slot& slot)
{
    if (slot.count != nullptr) {
        return static_cast<double>(*slot.count);
    }
    return slot.keepSign ? std::abs(*slot.value) : *slot.value;
}

bool sameNumber(double a, double b)
{
    return std::abs(a - b) <= kSameNumber * std::max(1.0, std::abs(a));
}

std::vector<std::string> namesIn(std::string_view text)
{
    return evaluateParameter(text, {}).names;
}

bool names(std::string_view text, std::string_view name)
{
    const std::vector<std::string> found = namesIn(text);
    return std::ranges::find(found, name) != found.end();
}

struct Table {
    ParameterValues values;
    std::vector<std::string> canonical;
    std::optional<ExpressionError> error;
};

Table evaluateTable(const Document& document)
{
    Table table;
    const std::vector<Parameter>& parameters = document.parameters();
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        const Parameter& parameter = parameters[i];
        const std::string label = "Parameter " + parameter.name;
        if (std::optional<ExpressionError> invalid = checkParameterName(parameter.name)) {
            table.error = labelled(label, *invalid);
            return table;
        }
        if (table.values.contains(parameter.name) || document.featureNamed(parameter.name) != nullptr) {
            table.error = ExpressionError{"duplicate", "'" + parameter.name + "' is already defined",
                                          "Every parameter and feature needs its own name"};
            return table;
        }
        ExpressionScope scope{&table.values, {}};
        for (std::size_t j = i + 1; j < parameters.size(); ++j) {
            scope.later.push_back(parameters[j].name);
        }
        const ExpressionResult result = evaluateParameter(parameter.expression, scope);
        if (!result.ok()) {
            table.error = labelled(label, *result.error);
            return table;
        }
        table.values[parameter.name] = {result.value, result.dim, result.anyDim};
        table.canonical.push_back(result.canonical);
    }
    return table;
}

bool reaches(const std::map<std::string, std::vector<std::string>, std::less<>>& refs, const std::string& from,
             const std::string& target)
{
    std::vector<std::string> pending{from};
    std::vector<std::string> seen;
    while (!pending.empty()) {
        const std::string name = pending.back();
        pending.pop_back();
        if (name == target) {
            return true;
        }
        if (std::ranges::find(seen, name) != seen.end()) {
            continue;
        }
        seen.push_back(name);
        if (const auto found = refs.find(name); found != refs.end()) {
            pending.insert(pending.end(), found->second.begin(), found->second.end());
        }
    }
    return false;
}

std::optional<ExpressionError> reorder(std::vector<Parameter>& parameters, std::size_t edited)
{
    std::map<std::string, std::vector<std::string>, std::less<>> refs;
    for (const Parameter& parameter : parameters) {
        refs[parameter.name] = namesIn(parameter.expression);
    }
    const std::string& name = parameters[edited].name;
    const std::vector<std::string>& own = refs[name];
    if (std::ranges::find(own, name) != own.end()) {
        return ExpressionError{"cycle", name + " names itself", {}};
    }
    std::vector<Parameter> ordered;
    std::vector<bool> placed(parameters.size(), false);
    const auto isPlaced = [&](const std::string& other) {
        const auto it = std::ranges::find(parameters, other, &Parameter::name);
        return it == parameters.end() || placed[static_cast<std::size_t>(it - parameters.begin())];
    };
    while (ordered.size() < parameters.size()) {
        bool progressed = false;
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            if (!placed[i] && std::ranges::all_of(refs[parameters[i].name], isPlaced)) {
                placed[i] = true;
                ordered.push_back(parameters[i]);
                progressed = true;
                break;
            }
        }
        if (!progressed) {
            for (const std::string& other : own) {
                if (reaches(refs, other, name)) {
                    std::string message = name;
                    message += " and " + other + " name each other";
                    return ExpressionError{"cycle", message, {}};
                }
            }
            return ExpressionError{"cycle", "Parameters name each other", {}};
        }
    }
    parameters = std::move(ordered);
    return std::nullopt;
}

std::optional<ExpressionError> commit(Document& document, Document candidate)
{
    if (std::optional<ExpressionError> error = applyParameters(candidate)) {
        return error;
    }
    document = std::move(candidate);
    return std::nullopt;
}

} // namespace

bool takesExpression(sketch::ConstraintKind kind)
{
    switch (kind) {
    case sketch::ConstraintKind::Distance:
    case sketch::ConstraintKind::HorizontalDistance:
    case sketch::ConstraintKind::VerticalDistance:
    case sketch::ConstraintKind::Radius:
    case sketch::ConstraintKind::Diameter:
    case sketch::ConstraintKind::Angle:
    case sketch::ConstraintKind::Offset:
        return true;
    case sketch::ConstraintKind::Coincident:
    case sketch::ConstraintKind::Horizontal:
    case sketch::ConstraintKind::Vertical:
    case sketch::ConstraintKind::Parallel:
    case sketch::ConstraintKind::Perpendicular:
    case sketch::ConstraintKind::Tangent:
    case sketch::ConstraintKind::Equal:
    case sketch::ConstraintKind::PointOnCurve:
    case sketch::ConstraintKind::Midpoint:
    case sketch::ConstraintKind::Symmetric:
        return false;
    }
    return false;
}

namespace {

ExpressionError damaged(const std::string& label, const std::string& text, const std::string& canonical)
{
    if (canonical.empty()) {
        return {"stored", label + " stores '" + text + "', which names no parameter", {}};
    }
    return {"stored", label + " stores '" + text + "' instead of '" + canonical + "'", {}};
}

std::optional<ExpressionError> stampAll(Document& document, bool exact)
{
    Table table = evaluateTable(document);
    if (table.error) {
        return table.error;
    }
    if (exact) {
        const std::vector<Parameter>& stored = document.parameters();
        for (std::size_t i = 0; i < stored.size(); ++i) {
            if (stored[i].expression != table.canonical[i]) {
                return damaged("Parameter " + stored[i].name, stored[i].expression, table.canonical[i]);
            }
        }
    }
    struct Stamp {
        Slot slot;
        double number = 0.0;
        std::string* text = nullptr;
        std::string canonical;
    };
    std::vector<Stamp> stamps;
    const ExpressionScope scope{&table.values, {}};
    const auto stampOf = [&](const Slot& slot, std::string& text, const std::string& label) {
        const ExpressionResult result = evaluateField(text, slot.quantity, scope);
        if (!result.ok()) {
            return std::optional(labelled(label, *result.error));
        }
        if (exact && (result.names.empty() || result.canonical != text)) {
            return std::optional(damaged(label, text, result.names.empty() ? std::string() : result.canonical));
        }
        if (std::optional<ExpressionError> refused = refusal(slot, result.value, label)) {
            return refused;
        }
        stamps.push_back({slot, result.value, &text, result.canonical});
        return std::optional<ExpressionError>();
    };
    std::vector<FeatureId> ids;
    for (const Feature& feature : document.features()) {
        ids.push_back(feature.id);
    }
    for (const FeatureId id : ids) {
        Feature& feature = *document.find(id);
        for (auto& [key, text] : feature.expressions) {
            const std::optional<Slot> slot = slotOf(feature.data, key);
            if (!slot) {
                return ExpressionError{"field", feature.name + " has no value called " + key, {}};
            }
            if (std::optional<ExpressionError> error = stampOf(*slot, text, fieldLabel(feature, key))) {
                return error;
            }
        }
        auto* data = std::get_if<SketchFeature>(&feature.data);
        if (data == nullptr) {
            continue;
        }
        std::vector<sketch::Id> constraints;
        for (const auto& [constraintId, constraint] : data->sketch.constraints()) {
            if (!constraint.expression.empty()) {
                constraints.push_back(constraintId);
            }
        }
        for (const sketch::Id constraintId : constraints) {
            sketch::Constraint& constraint = *data->sketch.constraint(constraintId);
            const std::string label = constraintLabel(feature, constraint);
            const std::optional<Slot> slot = slotOf(constraint);
            if (!slot) {
                return ExpressionError{"field", label + " takes no expression", {}};
            }
            if (std::optional<ExpressionError> error = stampOf(*slot, constraint.expression, label)) {
                return error;
            }
        }
    }
    for (const Stamp& stamp : stamps) {
        write(stamp.slot, stamp.number);
        *stamp.text = stamp.canonical;
    }
    for (const FeatureId id : ids) {
        std::erase_if(document.find(id)->expressions, [](const auto& entry) { return entry.second.empty(); });
    }
    std::vector<Parameter> parameters = document.parameters();
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        parameters[i].expression = table.canonical[i];
    }
    document.setParameters(std::move(parameters));
    return std::nullopt;
}

} // namespace

std::optional<ExpressionError> applyParameters(Document& document)
{
    return stampAll(document, false);
}

std::optional<ExpressionError> applyStoredParameters(Document& document)
{
    return stampAll(document, true);
}

void reconcileExpressions(const Document& before, Document& after)
{
    const std::optional<ParameterValues> values = parameterValues(after);
    const ExpressionScope scope{values ? &*values : nullptr, {}};
    const auto keeps = [&](const Slot& slot, const std::string& text, const std::string* earlierText,
                           std::optional<double> earlierNumber) {
        const double now = numberIn(slot);
        if (earlierText != nullptr && *earlierText == text && earlierNumber && sameNumber(now, *earlierNumber)) {
            return true;
        }
        const ExpressionResult result = evaluateField(text, slot.quantity, scope);
        return !result.ok() || sameNumber(now, result.value);
    };
    std::vector<FeatureId> ids;
    for (const Feature& feature : after.features()) {
        ids.push_back(feature.id);
    }
    for (const FeatureId id : ids) {
        Feature& feature = *after.find(id);
        const Feature* earlier = before.find(id);
        std::optional<FeatureData> earlierData;
        if (earlier != nullptr && !earlier->expressions.empty()) {
            earlierData = earlier->data;
        }
        std::erase_if(feature.expressions, [&](const auto& entry) {
            const std::optional<Slot> slot = slotOf(feature.data, entry.first);
            if (!slot) {
                return true;
            }
            const std::string* earlierText = nullptr;
            std::optional<double> earlierNumber;
            if (earlierData) {
                if (const auto found = earlier->expressions.find(entry.first); found != earlier->expressions.end()) {
                    earlierText = &found->second;
                    if (const std::optional<Slot> old = slotOf(*earlierData, entry.first)) {
                        earlierNumber = numberIn(*old);
                    }
                }
            }
            return !keeps(*slot, entry.second, earlierText, earlierNumber);
        });
        auto* data = std::get_if<SketchFeature>(&feature.data);
        if (data == nullptr) {
            continue;
        }
        const auto* earlierSketch = earlier != nullptr ? std::get_if<SketchFeature>(&earlier->data) : nullptr;
        std::vector<sketch::Id> constraints;
        for (const auto& [constraintId, constraint] : data->sketch.constraints()) {
            if (!constraint.expression.empty()) {
                constraints.push_back(constraintId);
            }
        }
        for (const sketch::Id constraintId : constraints) {
            sketch::Constraint& constraint = *data->sketch.constraint(constraintId);
            const std::optional<Slot> slot = slotOf(constraint);
            const sketch::Constraint* old =
                earlierSketch != nullptr ? earlierSketch->sketch.constraint(constraintId) : nullptr;
            const bool sameKind = old != nullptr && old->kind == constraint.kind;
            if (!slot || !keeps(*slot, constraint.expression, sameKind ? &old->expression : nullptr,
                                sameKind ? std::optional(old->value) : std::nullopt)) {
                constraint.expression.clear();
            }
        }
    }
}

std::optional<ParameterValues> parameterValues(const Document& document)
{
    Table table = evaluateTable(document);
    if (table.error) {
        return std::nullopt;
    }
    return std::move(table.values);
}

std::optional<ExpressionError> checkNewParameterName(const Document& document, std::string_view name)
{
    if (std::optional<ExpressionError> invalid = checkParameterName(name)) {
        return invalid;
    }
    if (document.parameter(name) != nullptr) {
        return ExpressionError{"duplicate", "'" + std::string(name) + "' is already a parameter", "Pick another name"};
    }
    if (document.featureNamed(name) != nullptr) {
        return ExpressionError{"duplicate", "'" + std::string(name) + "' is a feature name", "Pick another name"};
    }
    return std::nullopt;
}

std::optional<ExpressionError> addParameter(Document& document, const std::string& name, std::string_view expression)
{
    if (std::optional<ExpressionError> invalid = checkNewParameterName(document, name)) {
        return invalid;
    }
    Document candidate = document;
    std::vector<Parameter> parameters = candidate.parameters();
    parameters.push_back({name, std::string(expression)});
    candidate.setParameters(std::move(parameters));
    return commit(document, std::move(candidate));
}

std::optional<ExpressionError> setParameterExpression(Document& document, std::string_view name,
                                                      std::string_view expression)
{
    const auto& current = document.parameters();
    const auto found = std::ranges::find(current, name, &Parameter::name);
    if (found == current.end()) {
        return ExpressionError{"unknown-name", "Unknown name '" + std::string(name) + "'", {}};
    }
    const auto edited = static_cast<std::size_t>(found - current.begin());
    std::vector<Parameter> parameters = current;
    parameters[edited].expression = std::string(expression);
    if (std::optional<ExpressionError> error = reorder(parameters, edited)) {
        return error;
    }
    Document candidate = document;
    candidate.setParameters(std::move(parameters));
    return commit(document, std::move(candidate));
}

std::optional<ExpressionError> renameParameter(Document& document, std::string_view from, const std::string& to)
{
    if (document.parameter(from) == nullptr) {
        return ExpressionError{"unknown-name", "Unknown name '" + std::string(from) + "'", {}};
    }
    if (from == to) {
        return std::nullopt;
    }
    if (std::optional<ExpressionError> invalid = checkNewParameterName(document, to)) {
        return invalid;
    }
    const auto rewrite = [&](std::string& text) {
        if (names(text, from)) {
            text = renameInExpression(text, from, to);
        }
    };
    Document candidate = document;
    std::vector<Parameter> parameters = candidate.parameters();
    for (Parameter& parameter : parameters) {
        if (parameter.name == from) {
            parameter.name = to;
        }
        rewrite(parameter.expression);
    }
    candidate.setParameters(std::move(parameters));
    for (const Feature& feature : document.features()) {
        Feature& target = *candidate.find(feature.id);
        for (auto& entry : target.expressions) {
            rewrite(entry.second);
        }
        if (auto* data = std::get_if<SketchFeature>(&target.data)) {
            for (const auto& entry : data->sketch.constraints()) {
                rewrite(data->sketch.constraint(entry.first)->expression);
            }
        }
    }
    return commit(document, std::move(candidate));
}

std::optional<ExpressionError> removeParameter(Document& document, std::string_view name)
{
    if (document.parameter(name) == nullptr) {
        return ExpressionError{"unknown-name", "Unknown name '" + std::string(name) + "'", {}};
    }
    const std::vector<std::string> users = usersOf(document, name);
    if (!users.empty()) {
        return ExpressionError{"used", "Used by " + joined(users), {}};
    }
    Document candidate = document;
    std::vector<Parameter> parameters = candidate.parameters();
    std::erase_if(parameters, [&](const Parameter& parameter) { return parameter.name == name; });
    candidate.setParameters(std::move(parameters));
    return commit(document, std::move(candidate));
}

std::optional<ExpressionError> setFieldExpression(Document& document, const FieldRef& field, std::string_view text)
{
    Document candidate = document;
    Feature* feature = candidate.find(field.feature);
    if (feature == nullptr) {
        return ExpressionError{"field", "The feature is gone", {}};
    }
    std::optional<Slot> slot;
    std::string* stored = nullptr;
    std::string label;
    if (field.constraint != 0) {
        auto* data = std::get_if<SketchFeature>(&feature->data);
        sketch::Constraint* constraint = data != nullptr ? data->sketch.constraint(field.constraint) : nullptr;
        if (constraint != nullptr) {
            slot = slotOf(*constraint);
        }
        if (!slot) {
            return ExpressionError{"field", "This dimension takes no expression", {}};
        }
        stored = &constraint->expression;
        label = constraintLabel(*feature, *constraint);
    } else {
        slot = slotOf(feature->data, field.key);
        if (!slot) {
            return ExpressionError{"field", feature->name + " has no value called " + field.key, {}};
        }
        stored = &feature->expressions[field.key];
        label = fieldLabel(*feature, field.key);
    }
    const auto clear = [&]() {
        stored->clear();
        std::erase_if(feature->expressions, [](const auto& entry) { return entry.second.empty(); });
    };
    if (std::ranges::all_of(text, [](char c) { return c == ' ' || c == '\t'; })) {
        clear();
        return commit(document, std::move(candidate));
    }
    const std::optional<ParameterValues> values = parameterValues(candidate);
    const ExpressionScope scope{values ? &*values : nullptr, {}};
    const ExpressionResult result = evaluateField(text, slot->quantity, scope);
    if (!result.ok()) {
        return result.error;
    }
    if (std::optional<ExpressionError> refused = refusal(*slot, result.value, label)) {
        return refused;
    }
    if (result.canonical.empty()) {
        write(*slot, result.value);
        clear();
    } else {
        *stored = result.canonical;
    }
    return commit(document, std::move(candidate));
}

std::vector<std::string> usersOf(const Document& document, std::string_view name)
{
    std::vector<std::string> users;
    for (const Parameter& parameter : document.parameters()) {
        if (parameter.name != name && names(parameter.expression, name)) {
            users.push_back(parameter.name);
        }
    }
    for (const Feature& feature : document.features()) {
        for (const auto& [key, text] : feature.expressions) {
            if (names(text, name)) {
                users.push_back(fieldLabel(feature, key));
            }
        }
        if (const auto* data = std::get_if<SketchFeature>(&feature.data)) {
            for (const auto& entry : data->sketch.constraints()) {
                const sketch::Constraint& constraint = entry.second;
                if (!constraint.expression.empty() && names(constraint.expression, name)) {
                    users.push_back(constraintLabel(feature, constraint));
                }
            }
        }
    }
    return users;
}

} // namespace kalve137::core
