// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kernel/shapes.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace {

using namespace kalve137;

kernel::Segment line(kernel::Point2 a, kernel::Point2 b)
{
    kernel::Segment s;
    s.a = a;
    s.b = b;
    return s;
}

kernel::Segment arc(kernel::Point2 a, kernel::Point2 m, kernel::Point2 b)
{
    kernel::Segment s;
    s.kind = kernel::Segment::Kind::Arc;
    s.a = a;
    s.m = m;
    s.b = b;
    return s;
}

std::vector<model::Vec3> sides(const kernel::Prism& prism, std::size_t segments)
{
    std::vector<model::Vec3> out(segments);
    std::vector<int> seen(segments, 0);
    int starts = 0;
    int ends = 0;
    for (const kernel::PrismFace& face : prism.faces) {
        if (face.kind == kernel::PrismFace::Kind::Start) {
            ++starts;
        } else if (face.kind == kernel::PrismFace::Kind::End) {
            ++ends;
        } else {
            out.at(static_cast<std::size_t>(face.segment)) = face.outward;
            ++seen.at(static_cast<std::size_t>(face.segment));
        }
    }
    EXPECT_EQ(starts, 1);
    EXPECT_EQ(ends, 1);
    for (const int count : seen) {
        EXPECT_EQ(count, 1);
    }
    return out;
}

void expectNear(model::Vec3 actual, model::Vec3 expected)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-9);
    EXPECT_NEAR(actual.y, expected.y, 1e-9);
    EXPECT_NEAR(actual.z, expected.z, 1e-9);
}

TEST(PrismFactsTest, SidesReportTheirSegmentAndOutwardDirection)
{
    const model::Frame xy = *model::datumFrame("XY");
    const kernel::Loop counter{line({0, 0}, {20, 0}), line({20, 0}, {20, 10}), line({20, 10}, {0, 10}),
                               line({0, 10}, {0, 0})};
    const std::vector<model::Vec3> ccw = sides(kernel::prism(xy, counter, {}, {0, 0, 5}), 4);
    expectNear(ccw[0], {0, -1, 0});
    expectNear(ccw[1], {1, 0, 0});
    expectNear(ccw[2], {0, 1, 0});
    expectNear(ccw[3], {-1, 0, 0});

    const kernel::Loop clockwise{line({0, 0}, {0, 10}), line({0, 10}, {20, 10}), line({20, 10}, {20, 0}),
                                 line({20, 0}, {0, 0})};
    const std::vector<model::Vec3> cw = sides(kernel::prism(xy, clockwise, {}, {0, 0, 5}), 4);
    expectNear(cw[0], {-1, 0, 0});
    expectNear(cw[1], {0, 1, 0});
    expectNear(cw[2], {1, 0, 0});
    expectNear(cw[3], {0, -1, 0});
}

TEST(PrismFactsTest, ArcsFaceAwayFromTheInside)
{
    const model::Frame xy = *model::datumFrame("XY");
    const kernel::Loop bulge{line({0, 0}, {20, 0}), arc({20, 0}, {30, 5}, {20, 10}), line({20, 10}, {0, 10}),
                             line({0, 10}, {0, 0})};
    expectNear(sides(kernel::prism(xy, bulge, {}, {0, 0, 5}), 4)[1], {1, 0, 0});

    const kernel::Loop dent{line({0, 0}, {20, 0}), arc({20, 0}, {15, 5}, {20, 10}), line({20, 10}, {0, 10}),
                            line({0, 10}, {0, 0})};
    expectNear(sides(kernel::prism(xy, dent, {}, {0, 0, 5}), 4)[1], {1, 0, 0});
}

} // namespace
