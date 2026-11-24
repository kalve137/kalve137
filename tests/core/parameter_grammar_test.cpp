// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/parameters.h"

#include <gtest/gtest.h>

#include <numbers>
#include <string_view>

using namespace kalve137::core;

namespace {

constexpr double kTolerance = 1e-9;
constexpr double kPi = std::numbers::pi;
constexpr Dim kArea{2, 0};
constexpr Dim kVolume{3, 0};
constexpr std::string_view kAppOnly = "app-only";

struct Row {
    std::string_view text;
    double value = 0.0;
    Dim dim;
    std::string_view code;
    std::string_view canonical;
    std::string_view languageCode;
};

const Row kRows[] = {
    {"40 mm", 40.0, kLengthDim, {}, {}, {}},
    {"2.5 cm", 25.0, kLengthDim, {}, {}, {}},
    {"1 m", 1000.0, kLengthDim, {}, {}, {}},
    {"1 in", 25.4, kLengthDim, {}, {}, {}},
    {"90 deg", 90.0, kAngleDim, {}, {}, {}},
    {"1 rad", 180.0 / kPi, kAngleDim, {}, {}, {}},
    {"3 mm2", 3.0, kArea, {}, {}, {}},
    {"1 cm2", 100.0, kArea, {}, {}, {}},
    {"2 mm3", 2.0, kVolume, {}, {}, {}},
    {"1 cm3", 1000.0, kVolume, {}, {}, {}},
    {"4", 4.0, kPlainDim, {}, {}, {}},
    {"0", 0.0, kPlainDim, {}, {}, {}},
    {".5 mm", 0.5, kLengthDim, {}, "0.5 mm", {}},
    {"w", 40.0, kLengthDim, {}, {}, {}},
    {"_g2", 9.81, kPlainDim, {}, {}, {}},
    {"w * 2", 80.0, kLengthDim, {}, {}, {}},
    {"2*w", 80.0, kLengthDim, {}, "2 * w", {}},
    {"w / 2", 20.0, kLengthDim, {}, {}, {}},
    {"-w", -40.0, kLengthDim, {}, {}, {}},
    {"(w + h) / 2", 30.0, kLengthDim, {}, {}, {}},
    {"w - h - 5 mm", 15.0, kLengthDim, {}, {}, {}},
    {"w - (h - 5 mm)", 25.0, kLengthDim, {}, {}, {}},
    {"((w))", 40.0, kLengthDim, {}, "w", {}},
    {"w+h", 60.0, kLengthDim, {}, "w + h", {}},
    {"w * h", 800.0, kArea, {}, {}, {}},
    {"w * h * h", 16000.0, kVolume, {}, {}, {}},
    {"w / h", 2.0, kPlainDim, {}, {}, {}},
    {"a * 2", 60.0, kAngleDim, {}, {}, {}},
    {"n * w", 160.0, kLengthDim, {}, {}, {}},
    {"w + 0", 40.0, kLengthDim, {}, {}, {}},
    {"w + 5", 45.0, kLengthDim, {}, "w + 5 mm", "units"},
    {"5 - w", -35.0, kLengthDim, {}, "5 mm - w", "units"},
    {"w * 2 + 5", 85.0, kLengthDim, {}, "w * 2 + 5 mm", "units"},
    {"w + -5", 35.0, kLengthDim, {}, "w + -5 mm", "units"},
    {"w + 5 + 3", 48.0, kLengthDim, {}, "w + 5 mm + 3 mm", "units"},
    {"5 + 3 + w", 48.0, kLengthDim, {}, "5 mm + 3 mm + w", "units"},
    {"5 - 3 - w", -38.0, kLengthDim, {}, "5 mm - 3 mm - w", "units"},
    {"-5 + w", 35.0, kLengthDim, {}, "-5 mm + w", "units"},
    {"w - (5 - 3)", 38.0, kLengthDim, {}, "w - (5 mm - 3 mm)", "units"},
    {"z0 + w", 40.0, kLengthDim, {}, {}, {}},
    {"w - z0", 40.0, kLengthDim, {}, {}, {}},
    {"z0 + a", 30.0, kAngleDim, {}, {}, {}},
    {"-5 mm", -5.0, kLengthDim, {}, {}, {}},
    {"-(w + h)", -60.0, kLengthDim, {}, {}, {}},
    {"-w * 2", -80.0, kLengthDim, {}, {}, {}},
    {"-(w * 2)", -80.0, kLengthDim, {}, "-(w * 2)", {}},
    {"w - -h", 60.0, kLengthDim, {}, {}, {}},
    {"--w", 40.0, kLengthDim, {}, {}, {}},
    {"w - -5 mm", 45.0, kLengthDim, {}, {}, {}},
    {"w / 2 / 2", 10.0, kLengthDim, {}, {}, {}},
    {"w / (2 / 2)", 40.0, kLengthDim, {}, {}, {}},
    {"w * 2 / 4", 20.0, kLengthDim, {}, {}, {}},
    {"w - h + 5 mm", 25.0, kLengthDim, {}, {}, {}},
    {"w - (h + 5 mm)", 15.0, kLengthDim, {}, {}, {}},
    {"123456789012345 mm", 123456789012345.0, kLengthDim, {}, {}, {}},
    {"100000000000000000000", 1e20, kPlainDim, {}, {}, {}},
    {"0.000000001 mm", 1e-9, kLengthDim, {}, {}, {}},
    {"1.50000 mm", 1.5, kLengthDim, {}, "1.5 mm", {}},
    {"a + 15", 45.0, kAngleDim, {}, "a + 15 deg", "units"},
    {"w * 1,5", 60.0, kLengthDim, {}, "w * 1.5", "syntax"},
    {"w + 2,5", 42.5, kLengthDim, {}, "w + 2.5 mm", "syntax"},
    {"w + a", 0.0, {}, "units", {}, {}},
    {"w + h * h", 0.0, {}, "units", {}, {}},
    {"w * w + 5", 0.0, {}, "units", {}, {}},
    {"w + 2 * 3", 0.0, {}, "units", {}, {}},
    {"x", 0.0, {}, "unknown-name", {}, {}},
    {"ww", 0.0, {}, "unknown-name", {}, {}},
    {"z", 0.0, {}, "unknown-name", {}, {}},
    {"w +", 0.0, {}, "syntax", {}, {}},
    {"(w", 0.0, {}, "syntax", {}, {}},
    {"w w", 0.0, {}, "syntax", {}, {}},
    {"w = 2", 0.0, {}, "syntax", {}, {}},
    {"", 0.0, {}, "syntax", {}, {}},
    {"mm", 0.0, {}, "reserved", {}, {}},
    {"1.2.3 mm", 0.0, {}, "number", {}, {}},
    {"0.00000000000000000000000000000000000000000001 mm", 0.0, {}, "number", {}, {}},
    {"pieces", 0.0, {}, "pick", {}, {}},
    {"4 pieces", 0.0, {}, "pick", {}, {}},
    {"n + 1 pieces", 0.0, {}, "pick", {}, {}},
    {"w / 2 pieces", 0.0, {}, "pick", {}, {}},
    {"(3 pieces)", 0.0, {}, "pick", {}, {}},
    {"w $ 2", 0.0, {}, "character", {}, {}},
    {"w # wide", 0.0, {}, "comment", {}, {}},
    {"w / 0", 0.0, {}, "build", {}, {}},
    {"w / (h - h)", 0.0, {}, "build", {}, {}},
    {"XY", 0.0, {}, "type", {}, kAppOnly},
    {"w.top", 0.0, {}, "type", {}, kAppOnly},
    {"M5", 0.0, {}, "thread", {}, kAppOnly},
    {"sqrt(w)", 0.0, {}, "function", {}, kAppOnly},
    {"pi * w", 0.0, {}, "function", {}, kAppOnly},
    {"PI * w", 0.0, {}, "function", {}, kAppOnly},
    {"Sqrt(w)", 0.0, {}, "function", {}, kAppOnly},
    {"w ^ 2", 0.0, {}, "function", {}, kAppOnly},
    {"sqrt(16) * 1 mm", 0.0, {}, "function", {}, kAppOnly},
};

const ParameterValues& values()
{
    static const ParameterValues kValues{
        {"w", {40.0, kLengthDim}}, {"h", {20.0, kLengthDim}},  {"a", {30.0, kAngleDim}},
        {"n", {4.0, kPlainDim}},   {"_g2", {9.81, kPlainDim}}, {"z0", {0.0, kPlainDim, true}},
    };
    return kValues;
}

ExpressionScope scope()
{
    return {&values(), {"z"}};
}

} // namespace

TEST(ParameterGrammar, TableOfExpressions)
{
    for (const Row& row : kRows) {
        SCOPED_TRACE(std::string(row.text));
        const ExpressionResult result = evaluateParameter(row.text, scope());
        if (!row.code.empty()) {
            ASSERT_FALSE(result.ok());
            EXPECT_EQ(result.error->code, row.code);
            EXPECT_FALSE(result.error->message.empty());
            EXPECT_TRUE(row.languageCode.empty() || row.languageCode == kAppOnly);
            continue;
        }
        EXPECT_NE(row.languageCode, kAppOnly);
        ASSERT_TRUE(result.ok()) << result.error->message;
        EXPECT_NEAR(result.value, row.value, kTolerance);
        EXPECT_EQ(result.dim, row.dim);
        EXPECT_EQ(result.canonical, row.canonical.empty() ? row.text : row.canonical);
        if (!row.languageCode.empty()) {
            EXPECT_NE(result.canonical, row.text);
            EXPECT_TRUE(row.languageCode == "units" || row.languageCode == "syntax");
            EXPECT_EQ(row.languageCode == "syntax", row.text.find(',') != std::string_view::npos);
        }
        const ExpressionResult again = evaluateParameter(result.canonical, scope());
        ASSERT_TRUE(again.ok());
        EXPECT_EQ(again.canonical, result.canonical);
        EXPECT_NEAR(again.value, result.value, kTolerance);
    }
}

TEST(ParameterGrammar, MessagesCarryTheLanguageHints)
{
    EXPECT_EQ(evaluateParameter("ww", scope()).error->hint, "Did you mean 'w'?");
    EXPECT_EQ(evaluateParameter("z", scope()).error->hint, "'z' is defined further down. Move it above this line");
    EXPECT_EQ(evaluateParameter("w + a", scope()).error->message, "Cannot add an angle to a length");
    EXPECT_TRUE(evaluateParameter("w + a", scope()).error->hint.empty());
    EXPECT_EQ(evaluateParameter("w * w + 5", scope()).error->hint, "Add a unit, like 5 mm2");
    EXPECT_EQ(evaluateParameter("w + 2 * 3", scope()).error->hint, "Add a unit, like 2 mm");
    EXPECT_EQ(evaluateParameter("w / 0", scope()).error->message, "Division by zero");
}

TEST(ParameterGrammar, NamesReachedAreListedOnce)
{
    const ExpressionResult result = evaluateParameter("w * 2 + h - w", scope());
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.names, (std::vector<std::string>{"w", "h"}));
    EXPECT_TRUE(evaluateParameter("40 mm", scope()).names.empty());
}

TEST(ParameterGrammar, FieldsTakeTheirOwnKind)
{
    const ExpressionResult doubled = evaluateField("w * 2", Quantity::Length, scope());
    ASSERT_TRUE(doubled.ok());
    EXPECT_NEAR(doubled.value, 80.0, kTolerance);
    EXPECT_EQ(doubled.canonical, "w * 2");

    const ExpressionResult forgiving = evaluateField("w + 5", Quantity::Length, scope());
    ASSERT_TRUE(forgiving.ok());
    EXPECT_NEAR(forgiving.value, 45.0, kTolerance);
    EXPECT_EQ(forgiving.canonical, "w + 5 mm");

    const ExpressionResult angle = evaluateField("a + 15", Quantity::Angle, scope());
    ASSERT_TRUE(angle.ok());
    EXPECT_NEAR(angle.value, 45.0, kTolerance);
    EXPECT_EQ(angle.canonical, "a + 15 deg");

    const ExpressionResult count = evaluateField("n + 1", Quantity::Count, scope());
    ASSERT_TRUE(count.ok());
    EXPECT_NEAR(count.value, 5.0, kTolerance);

    const ExpressionResult wrongKind = evaluateField("a", Quantity::Length, scope());
    ASSERT_FALSE(wrongKind.ok());
    EXPECT_EQ(wrongKind.error->code, "units");
    EXPECT_EQ(wrongKind.error->message, "Needs a length, not an angle");

    const ExpressionResult bareParam = evaluateField("n", Quantity::Length, scope());
    ASSERT_FALSE(bareParam.ok());
    EXPECT_EQ(bareParam.error->hint, "Parameter n has no unit. Add one where it is defined, like 10 mm");

    const ExpressionResult bareProduct = evaluateField("n * 2", Quantity::Length, scope());
    ASSERT_FALSE(bareProduct.ok());
    EXPECT_EQ(bareProduct.error->hint, "Write n * 2 mm");
}

TEST(ParameterGrammar, PlainValuesKeepTheOldDialect)
{
    const ExpressionResult comma = evaluateField("12,5", Quantity::Length, scope());
    ASSERT_TRUE(comma.ok());
    EXPECT_NEAR(comma.value, 12.5, kTolerance);
    EXPECT_TRUE(comma.canonical.empty());

    const ExpressionResult function = evaluateField("sqrt(16) + 2^2", Quantity::Length, scope());
    ASSERT_TRUE(function.ok());
    EXPECT_NEAR(function.value, 8.0, kTolerance);

    const ExpressionResult inches = evaluateField("1\"", Quantity::Length, scope());
    ASSERT_TRUE(inches.ok());
    EXPECT_NEAR(inches.value, 25.4, kTolerance);

    const ExpressionResult refused = evaluateField("sqrt(w)", Quantity::Length, scope());
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error->code, "function");

    EXPECT_EQ(evaluateField("12 kg", Quantity::Length, scope()).error->code, "syntax");
    EXPECT_EQ(evaluateField("12 +", Quantity::Length, scope()).error->code, "value");
}

TEST(ParameterGrammar, NameDetection)
{
    EXPECT_FALSE(namesParameter("12,5"));
    EXPECT_FALSE(namesParameter("1e-3 mm"));
    EXPECT_FALSE(namesParameter("sqrt(2) * pi"));
    EXPECT_FALSE(namesParameter("3 ft + 2 um"));
    EXPECT_FALSE(namesParameter("SQRT(2) * PI"));
    EXPECT_TRUE(namesParameter("w"));
    EXPECT_TRUE(namesParameter("2 * w"));
}

TEST(ParameterGrammar, ParameterNames)
{
    EXPECT_FALSE(checkParameterName("w").has_value());
    EXPECT_FALSE(checkParameterName("_x1").has_value());
    EXPECT_FALSE(checkParameterName("Width").has_value());
    EXPECT_EQ(checkParameterName("").value().code, "name");
    EXPECT_EQ(checkParameterName("1w").value().code, "name");
    EXPECT_EQ(checkParameterName("w-1").value().code, "name");
    EXPECT_EQ(checkParameterName("M2.5").value().code, "name");
    EXPECT_EQ(checkParameterName("mm").value().code, "reserved");
    EXPECT_EQ(checkParameterName("M5").value().code, "reserved");
    EXPECT_EQ(checkParameterName("XY").value().code, "reserved");
    EXPECT_EQ(checkParameterName("pi").value().code, "reserved");
    EXPECT_EQ(checkParameterName("ft").value().code, "reserved");
    EXPECT_EQ(checkParameterName("pieces").value().code, "reserved");
    for (const std::string_view word : {"PI", "Pi", "SQRT", "Sin", "FT", "Um"}) {
        EXPECT_EQ(checkParameterName(word).value().code, "reserved") << word;
    }
    for (const std::string_view word : {"part",  "param",  "sketch", "on",   "rect", "circle", "center",   "size",
                                        "point", "line",   "arc",    "near", "from", "to",     "diameter", "extrude",
                                        "cut",   "fillet", "check",  "up",   "down", "depth",  "through",  "radius"}) {
        EXPECT_EQ(checkParameterName(word).value().code, "reserved") << word;
    }
    EXPECT_EQ(checkParameterName("hole").value().message, "'hole' is a kind of feature and cannot name a parameter");
    EXPECT_FALSE(checkParameterName("Depth").has_value()) << "Keywords match their case";
    EXPECT_EQ(checkParameterName("w\xC3\xA9").value().code, "name");
    EXPECT_EQ(checkParameterName("\xC3\xA9").value().code, "name");
}

TEST(ParameterGrammar, MessagesFitAValueField)
{
    EXPECT_EQ(evaluateParameter("w +", scope()).error->message, "Expected a value but found the end");
    const ExpressionResult comment = evaluateParameter("w # wide", scope());
    EXPECT_TRUE(comment.error->hint.empty());
    EXPECT_EQ(evaluateParameter("w\xC3\xA9", scope()).error->code, "character");
    const ExpressionResult pieces = evaluateParameter("4 pieces", scope());
    EXPECT_EQ(pieces.error->message, "'4 pieces' only counts the pieces a pick chooses among");
    EXPECT_EQ(pieces.error->hint, "Use a plain number here, like 4");
    EXPECT_EQ(evaluateField("4 pieces", Quantity::Count, scope()).error->code, "pick");
    EXPECT_EQ(evaluateField("4 pieces", Quantity::Count, {}).error->code, "pick");
    const ExpressionResult unreadable = evaluateField("5 +", Quantity::Length, {});
    EXPECT_EQ(unreadable.error->message, "Can't read this as a value");
    EXPECT_EQ(unreadable.error->hint, "Type a number or an expression, such as 30/2 or 1in");
}

TEST(ParameterGrammar, BareSumsTakeTheFieldUnit)
{
    const ExpressionResult before = evaluateField("5 + 3 + w", Quantity::Length, scope());
    ASSERT_TRUE(before.ok()) << before.error->message;
    EXPECT_NEAR(before.value, 48.0, kTolerance);
    EXPECT_EQ(before.canonical, "5 mm + 3 mm + w");
    const ExpressionResult angle = evaluateField("10 + 5 + a", Quantity::Angle, scope());
    ASSERT_TRUE(angle.ok()) << angle.error->message;
    EXPECT_NEAR(angle.value, 45.0, kTolerance);
    EXPECT_EQ(evaluateParameter("2 * 3 + w", scope()).error->code, "units");
}

TEST(ParameterGrammar, AZeroParameterFitsAnyUnit)
{
    const ExpressionResult length = evaluateField("z0", Quantity::Length, scope());
    ASSERT_TRUE(length.ok()) << length.error->message;
    EXPECT_EQ(length.value, 0.0);
    const ExpressionResult angle = evaluateField("a + z0", Quantity::Angle, scope());
    ASSERT_TRUE(angle.ok()) << angle.error->message;
    EXPECT_NEAR(angle.value, 30.0, kTolerance);
    EXPECT_TRUE(evaluateParameter("z0", scope()).anyDim);
    EXPECT_FALSE(evaluateParameter("n - n", scope()).anyDim);
}

TEST(ParameterGrammar, RenameRewritesOnlyThatName)
{
    EXPECT_EQ(renameInExpression("w * 2 + wide - (w - h)", "w", "width"), "width * 2 + wide - (width - h)");
    EXPECT_EQ(renameInExpression("h / 2", "w", "width"), "h / 2");
}

TEST(ParameterGrammar, NumbersFormatShortAndExact)
{
    EXPECT_EQ(formatNumber(40.0), "40");
    EXPECT_EQ(formatNumber(0.1), "0.1");
    EXPECT_EQ(formatNumber(2.5), "2.5");
    EXPECT_EQ(formatNumber(1e-7), "0.0000001");
    EXPECT_EQ(formatNumber(-0.0), "0");
    EXPECT_EQ(formatNumber(1e20), "100000000000000000000");
    EXPECT_EQ(formatNumber(1e-30), "0.000000000000000000000000000001");
    for (const double value : {1e300, -1e300, 1e-45, 1.2345678901234567e-35, 5e-324}) {
        const std::string text = formatNumber(value);
        EXPECT_EQ(text.find_first_of("eE"), std::string::npos) << text;
    }
    const ExpressionResult third = evaluateParameter(formatNumber(1.0 / 3.0), scope());
    ASSERT_TRUE(third.ok());
    EXPECT_EQ(third.value, 1.0 / 3.0);
}

TEST(ParameterGrammar, ValuesFormatWithTheirUnit)
{
    EXPECT_EQ(formatParameterValue({40.0, kLengthDim}), "40 mm");
    EXPECT_EQ(formatParameterValue({90.0, kAngleDim}), "90 deg");
    EXPECT_EQ(formatParameterValue({4.0, kPlainDim}), "4");
    EXPECT_EQ(formatParameterValue({800.0, kArea}), "800 mm2");
    EXPECT_EQ(formatParameterValue({1.0 / 3.0, kLengthDim}), "0.333 mm");
}

TEST(ParameterGrammar, DegreesComeBackExact)
{
    for (const double degrees : {30.0, 0.1, 17.3, 45.0, 359.9}) {
        const ExpressionResult typed = evaluateParameter(formatNumber(degrees) + " deg", {});
        ASSERT_TRUE(typed.ok());
        const ParameterValues values{{"tilt", {typed.value, typed.dim}}};
        const ExpressionScope scope{&values, {}};
        EXPECT_EQ(evaluateField("tilt", Quantity::Angle, scope).value, degrees);
        EXPECT_EQ(evaluateField("tilt / 2", Quantity::Angle, scope).value, degrees / 2.0);
        EXPECT_EQ(formatParameterValue(values.at("tilt")), formatNumber(degrees) + " deg");
    }
}
