// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/expression.h"

#include <gtest/gtest.h>

using kalve137::core::evaluate;
using kalve137::core::Quantity;

namespace {

constexpr double kTolerance = 1e-9;

double length(std::string_view text)
{
    return evaluate(text, Quantity::Length).value_or(-1.0);
}

} // namespace

TEST(Expression, PlainNumbers)
{
    EXPECT_NEAR(length("12"), 12.0, kTolerance);
    EXPECT_NEAR(length(" 12.5 "), 12.5, kTolerance);
    EXPECT_NEAR(length("12,5"), 12.5, kTolerance);
    EXPECT_NEAR(length(".5"), 0.5, kTolerance);
    EXPECT_NEAR(length("1e-3"), 0.001, kTolerance);
}

TEST(Expression, Arithmetic)
{
    EXPECT_NEAR(length("30/2"), 15.0, kTolerance);
    EXPECT_NEAR(length("2 + 3 * 4"), 14.0, kTolerance);
    EXPECT_NEAR(length("(2 + 3) * 4"), 20.0, kTolerance);
    EXPECT_NEAR(length("-5 + 8"), 3.0, kTolerance);
    EXPECT_NEAR(length("2^3^2"), 512.0, kTolerance);
    EXPECT_NEAR(length("sqrt(9) + pi - pi"), 3.0, kTolerance);
}

TEST(Expression, LengthUnits)
{
    EXPECT_NEAR(length("12 mm"), 12.0, kTolerance);
    EXPECT_NEAR(length("1in"), 25.4, kTolerance);
    EXPECT_NEAR(length("1\""), 25.4, kTolerance);
    EXPECT_NEAR(length("2 cm + 5"), 25.0, kTolerance);
    EXPECT_NEAR(length("0.1m"), 100.0, kTolerance);
    EXPECT_NEAR(length("1 ft / 12"), 25.4, kTolerance);
}

TEST(Expression, AngleUnits)
{
    EXPECT_NEAR(*evaluate("45", Quantity::Angle), 45.0, kTolerance);
    EXPECT_NEAR(*evaluate("45\xC2\xB0", Quantity::Angle), 45.0, kTolerance);
    EXPECT_NEAR(*evaluate("pi rad", Quantity::Angle), 180.0, kTolerance);
    EXPECT_NEAR(*evaluate("90 deg / 2", Quantity::Angle), 45.0, kTolerance);
}

TEST(Expression, Trigonometry)
{
    EXPECT_NEAR(length("10 * sin(30)"), 5.0, kTolerance);
    EXPECT_NEAR(length("cos(pi rad)"), -1.0, kTolerance);
}

TEST(Expression, Rejects)
{
    EXPECT_FALSE(evaluate("", Quantity::Length));
    EXPECT_FALSE(evaluate("abc", Quantity::Length));
    EXPECT_FALSE(evaluate("1/0", Quantity::Length));
    EXPECT_FALSE(evaluate("(1 + 2", Quantity::Length));
    EXPECT_FALSE(evaluate("5 deg", Quantity::Length));
    EXPECT_FALSE(evaluate("5 mm", Quantity::Angle));
    EXPECT_FALSE(evaluate("5 mm", Quantity::Count));
    EXPECT_FALSE(evaluate("5 5", Quantity::Length));
}
