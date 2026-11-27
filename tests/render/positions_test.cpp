// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/body_mesh.h"
#include "render/positions.h"

#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <vector>

namespace {

using kalve137::render::Position;
using kalve137::render::relativePositions;
using kalve137::snapshot::Mesh;
using kalve137::snapshot::Point;

constexpr Point kOrigin{1'000'000.0, 0.0, 0.0};
constexpr double kReconstructTolerance = 1e-4;
constexpr double kAreaTolerance = 0.01;
constexpr double kNormalTolerance = 0.999;

Mesh farPin()
{
    return kalve137::geometry::meshBody(
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(1'000'000.3, 0.7, 0.2), gp_Dir(0.0, 0.0, 1.0)), 2.0, 5.0).Shape());
}

Point minus(const Point& a, const Point& b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Point cross(const Point& a, const Point& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

double length(const Point& a)
{
    return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

std::vector<Point> widened(const std::vector<Position>& positions)
{
    std::vector<Point> points;
    points.reserve(positions.size());
    for (const Position& position : positions) {
        points.push_back({position[0], position[1], position[2]});
    }
    return points;
}

std::vector<Position> worldPositions(const Mesh& mesh)
{
    return relativePositions(mesh, Point{});
}

struct Report {
    double worstOffset = 0.0;
    std::size_t brokenTriangles = 0;
    std::size_t splitCorners = 0;
};

Report check(const Mesh& mesh, const std::vector<Position>& positions, const Point& origin)
{
    Report report;
    const std::vector<Point> drawn = widened(positions);
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        report.worstOffset = std::max(report.worstOffset, length(minus(minus(mesh.vertices[i], origin), drawn[i])));
    }
    for (const auto& triangle : mesh.triangles) {
        const Point exact = cross(minus(mesh.vertices[triangle[1]], mesh.vertices[triangle[0]]),
                                  minus(mesh.vertices[triangle[2]], mesh.vertices[triangle[0]]));
        const Point seen =
            cross(minus(drawn[triangle[1]], drawn[triangle[0]]), minus(drawn[triangle[2]], drawn[triangle[0]]));
        const double exactArea = length(exact);
        const double seenArea = length(seen);
        const double alignment =
            seenArea > 0.0 ? (exact[0] * seen[0] + exact[1] * seen[1] + exact[2] * seen[2]) / (exactArea * seenArea)
                           : 0.0;
        if (std::abs(seenArea - exactArea) > kAreaTolerance * exactArea || alignment < kNormalTolerance) {
            ++report.brokenTriangles;
        }
    }
    std::map<Point, Position> cornerAt;
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        const auto [found, added] = cornerAt.try_emplace(mesh.vertices[i], positions[i]);
        if (!added && found->second != positions[i]) {
            ++report.splitCorners;
        }
    }
    return report;
}

TEST(Positions, ABodyAKilometerAwayKeepsItsShapeRelativeToTheSceneOrigin)
{
    const Mesh mesh = farPin();
    ASSERT_FALSE(mesh.triangles.empty());
    const Report report = check(mesh, relativePositions(mesh, kOrigin), kOrigin);
    EXPECT_LT(report.worstOffset, kReconstructTolerance);
    EXPECT_EQ(report.brokenTriangles, 0U);
    EXPECT_EQ(report.splitCorners, 0U);
}

TEST(Positions, WorldOriginFloatsBreakTheSameBody)
{
    const Mesh mesh = farPin();
    const Report report = check(mesh, worldPositions(mesh), Point{});
    EXPECT_GT(report.worstOffset, 100.0 * kReconstructTolerance);
    EXPECT_GT(report.brokenTriangles, 0U);
}

} // namespace
