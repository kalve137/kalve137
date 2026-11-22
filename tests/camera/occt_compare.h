// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"

#include <gtest/gtest.h>

#include <Graphic3d_Camera.hxx>
#include <NCollection_Mat4.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_XYZ.hxx>

#include <algorithm>
#include <cmath>

namespace kalve137::test {

inline gp_Pnt toPnt(const camera::Vec3& v)
{
    return {v.x, v.y, v.z};
}

inline gp_Dir toDir(const camera::Vec3& v)
{
    return {v.x, v.y, v.z};
}

inline camera::Vec3 toVec(const gp_XYZ& v)
{
    return {v.X(), v.Y(), v.Z()};
}

inline void expectNear(const camera::Vec3& ours, const gp_XYZ& theirs, double tolerance)
{
    EXPECT_NEAR(ours.x, theirs.X(), tolerance);
    EXPECT_NEAR(ours.y, theirs.Y(), tolerance);
    EXPECT_NEAR(ours.z, theirs.Z(), tolerance);
}

inline void expectMatrix(const camera::Mat4& ours, const NCollection_Mat4<double>& theirs, double tolerance)
{
    double size = 1.0;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            size = std::max(size, std::abs(theirs.GetValue(row, column)));
        }
    }
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            EXPECT_NEAR(ours.at(row, column), theirs.GetValue(row, column), tolerance * size)
                << "row " << row << " column " << column;
        }
    }
}

inline void expectSame(const camera::Camera& ours, const Graphic3d_Camera& theirs, double tolerance)
{
    const double size = std::max({1.0, theirs.Distance(), theirs.Eye().XYZ().Modulus()});
    expectNear(ours.eye(), theirs.Eye().XYZ(), tolerance * size);
    expectNear(ours.center(), theirs.Center().XYZ(), tolerance * size);
    expectNear(ours.direction(), theirs.Direction().XYZ(), tolerance);
    expectNear(ours.up(), theirs.Up().XYZ(), tolerance);
    EXPECT_NEAR(ours.distance(), theirs.Distance(), tolerance * size);
    EXPECT_NEAR(ours.scale(), theirs.Scale(), tolerance * std::max(1.0, theirs.Scale()));
    EXPECT_EQ(ours.isOrthographic(), theirs.IsOrthographic());
    EXPECT_DOUBLE_EQ(ours.aspect(), theirs.Aspect());
    EXPECT_DOUBLE_EQ(ours.zNear(), theirs.ZNear());
    EXPECT_DOUBLE_EQ(ours.zFar(), theirs.ZFar());
}

} // namespace kalve137::test
