// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_view.h"

#include <gtest/gtest.h>

namespace kalve137::render {
namespace {

DeviceProbe openGl(int major, int minor, bool es)
{
    return {.created = true, .texelFetch = true, .openGl = true, .openGlEs = es, .major = major, .minor = minor};
}

TEST(StartProblem, ADeviceThatDoesNotStartIsNoDevice)
{
    EXPECT_EQ(startProblem({}), StartProblem::NoDevice);
}

TEST(StartProblem, TheShadersNeedTexelFetch)
{
    EXPECT_EQ(startProblem({.created = true, .texelFetch = true}), StartProblem::None);
    EXPECT_EQ(startProblem({.created = true, .texelFetch = false}), StartProblem::TooOld);
}

TEST(StartProblem, OpenGlNeedsThreePointTwoOrEsThree)
{
    EXPECT_EQ(startProblem(openGl(4, 6, false)), StartProblem::None);
    EXPECT_EQ(startProblem(openGl(3, 2, false)), StartProblem::None);
    EXPECT_EQ(startProblem(openGl(3, 1, false)), StartProblem::TooOld);
    EXPECT_EQ(startProblem(openGl(2, 1, false)), StartProblem::TooOld);
    EXPECT_EQ(startProblem(openGl(3, 0, true)), StartProblem::None);
    EXPECT_EQ(startProblem(openGl(2, 0, true)), StartProblem::TooOld);
}

} // namespace
} // namespace kalve137::render
