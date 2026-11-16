// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "model/vec.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kalve137::kernel {

struct KernelError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Shape {
public:
    struct Impl;

    Shape() = default;
    explicit Shape(std::shared_ptr<const Impl> impl) : m_impl(std::move(impl)) {}

    bool empty() const { return m_impl == nullptr; }
    int faceCount() const;
    int edgeCount() const;
    const Impl& impl() const { return *m_impl; }

private:
    std::shared_ptr<const Impl> m_impl;
};

struct Point2 {
    double u = 0.0;
    double v = 0.0;
};

struct Segment {
    enum class Kind { Line, Arc, Circle };

    Kind kind = Kind::Line;
    Point2 a;
    Point2 b;
    Point2 m;
    Point2 c;
    double radius = 0.0;
};

using Loop = std::vector<Segment>;

struct PrismFace {
    enum class Kind { Start, End, Side };

    Kind kind = Kind::Side;
    int segment = -1;
    model::Vec3 outward;
};

struct Prism {
    Shape shape;
    std::vector<PrismFace> faces;
};

Prism prism(const model::Frame& frame, const Loop& loop, model::Vec3 offset, model::Vec3 vector);

struct Origin {
    int input = -1;
    int face = -1;
    int edge = -1;
};

struct Result {
    Shape shape;
    std::vector<Origin> origins;
};

Result join(const Shape& a, const Shape& b);
Result cut(const Shape& a, const Shape& b);
Result round(const Shape& body, const std::vector<int>& edges, double radius);

struct FaceInfo {
    std::string kind;
    double area = 0.0;
    model::Vec3 centroid;
    model::Vec3 normal;
    model::Vec3 axis;
    double radius = 0.0;
    std::vector<model::Vec3> vertices;
    std::vector<std::array<int, 3>> triangles;
};

struct EdgeInfo {
    int faceA = -1;
    int faceB = -1;
    std::string kind;
    double length = 0.0;
    std::vector<model::Vec3> points;
};

struct Inspection {
    std::vector<FaceInfo> faces;
    std::vector<EdgeInfo> edges;
    double volume = 0.0;
    model::Vec3 boxMin;
    model::Vec3 boxMax;
    int solids = 0;
};

struct Detail {
    bool measures = true;
    bool meshes = false;
    std::vector<int> centroids;
};

Inspection inspect(const Shape& shape, Detail detail);

std::optional<model::Frame> faceFrame(const Shape& shape, int face);

struct Pick {
    bool edges = false;
    std::vector<int> items;
};

double distance(const Shape& shape, const Pick& a, const Pick& b);

void exportStep(const Shape& shape, const std::string& name, const std::filesystem::path& path);
std::vector<Shape> importStep(const std::filesystem::path& path);

} // namespace kalve137::kernel
