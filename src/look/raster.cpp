// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "look/raster.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace kalve137::look {

namespace {

using model::Vec3;

struct Rgb {
    double r;
    double g;
    double b;
};

constexpr Rgb kBackgroundTop{40, 45, 52};
constexpr Rgb kBackgroundBottom{22, 25, 29};
constexpr Rgb kPlane{178, 183, 190};
constexpr Rgb kCurved{164, 173, 186};
constexpr Rgb kHighlight{232, 176, 75};
constexpr Rgb kEdge{24, 27, 32};

std::uint32_t pack(Rgb c)
{
    const auto channel = [](double v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0, 255.0))); };
    return 0xFF000000u | (channel(c.r) << 16) | (channel(c.g) << 8) | channel(c.b);
}

Rgb mix(Rgb a, Rgb b, double t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

} // namespace

std::optional<View> namedView(const std::string& name)
{
    constexpr double kDegree = std::numbers::pi / 180.0;
    View view;
    if (name == "iso") {
        return view;
    }
    if (name == "iso-back") {
        view.yaw = 135.0 * kDegree;
        return view;
    }
    if (name == "iso-below") {
        view.pitch = -35.26 * kDegree;
        return view;
    }
    if (name == "top") {
        view.yaw = -90.0 * kDegree;
        view.pitch = 89.999 * kDegree;
        return view;
    }
    if (name == "front") {
        view.yaw = -90.0 * kDegree;
        view.pitch = 0.0;
        return view;
    }
    if (name == "right") {
        view.yaw = 0.0;
        view.pitch = 0.0;
        return view;
    }
    return std::nullopt;
}

View fitted(const model::Snapshot& snapshot, View view)
{
    const Vec3 size = snapshot.boxMax - snapshot.boxMin;
    view.fitSpan = std::max(1e-6, std::sqrt(model::dot(size, size)));
    view.fitCenter = (snapshot.boxMin + snapshot.boxMax) * 0.5;
    view.panX = 0.0;
    view.panY = 0.0;
    view.zoom = 1.0;
    return view;
}

int Frame::faceAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return -1;
    }
    return faces[static_cast<std::size_t>(y) * width + x];
}

Vec3 Frame::project(Vec3 point) const
{
    const Vec3 d = point - center;
    return {model::dot(d, right) * scale + width / 2.0, -model::dot(d, up) * scale + height / 2.0, model::dot(d, eye)};
}

Frame render(const model::Snapshot& snapshot, const View& view, int width, int height, const Style& style)
{
    Frame frame;
    frame.width = width;
    frame.height = height;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    frame.pixels.resize(count);
    frame.faces.assign(count, -1);
    frame.depth.assign(count, -1e30f);
    for (int y = 0; y < height; ++y) {
        const std::uint32_t color = pack(mix(kBackgroundTop, kBackgroundBottom, static_cast<double>(y) / height));
        std::fill_n(frame.pixels.begin() + static_cast<std::ptrdiff_t>(y) * width, width, color);
    }
    if (!snapshot.built) {
        return frame;
    }

    frame.eye = {std::cos(view.pitch) * std::cos(view.yaw), std::cos(view.pitch) * std::sin(view.yaw),
                 std::sin(view.pitch)};
    frame.right = {-std::sin(view.yaw), std::cos(view.yaw), 0.0};
    frame.up = model::normalized(model::cross(frame.right, frame.eye * -1.0));
    if (view.fitSpan > 0.0) {
        frame.center = view.fitCenter;
        frame.scale = std::min(width, height) / view.fitSpan * 0.78 * view.zoom;
    } else {
        frame.center = (snapshot.boxMin + snapshot.boxMax) * 0.5;
        double spanX = 1e-9;
        double spanY = 1e-9;
        for (int corner = 0; corner < 8; ++corner) {
            const Vec3 p{corner & 1 ? snapshot.boxMax.x : snapshot.boxMin.x,
                         corner & 2 ? snapshot.boxMax.y : snapshot.boxMin.y,
                         corner & 4 ? snapshot.boxMax.z : snapshot.boxMin.z};
            const Vec3 d = p - frame.center;
            spanX = std::max(spanX, 2.0 * std::abs(model::dot(d, frame.right)));
            spanY = std::max(spanY, 2.0 * std::abs(model::dot(d, frame.up)));
        }
        frame.scale = std::min(width / spanX, height / spanY) * 0.78 * view.zoom;
    }
    frame.center = frame.center - frame.right * (view.panX / frame.scale) + frame.up * (view.panY / frame.scale);

    const Vec3 light = model::normalized(frame.eye * 0.75 + frame.up * 0.55 - frame.right * 0.35);
    for (int faceIndex = 0; faceIndex < static_cast<int>(snapshot.faces.size()); ++faceIndex) {
        const model::Face& face = snapshot.faces[faceIndex];
        Rgb base = face.kind == "plane" ? kPlane : kCurved;
        if (style.highlighted.count(faceIndex) != 0) {
            base = kHighlight;
        } else if (style.hovered == faceIndex) {
            base = mix(base, kHighlight, 0.45);
        }
        std::vector<Vec3> screen;
        screen.reserve(face.vertices.size());
        for (const Vec3& v : face.vertices) {
            screen.push_back(frame.project(v));
        }
        std::vector<Vec3> smooth(face.vertices.size());
        for (const auto& triangle : face.triangles) {
            const Vec3& a3 = face.vertices[triangle[0]];
            const Vec3 n = model::cross(face.vertices[triangle[1]] - a3, face.vertices[triangle[2]] - a3);
            for (const int corner : triangle) {
                smooth[corner] = smooth[corner] + n;
            }
        }
        std::vector<double> shades(face.vertices.size());
        for (std::size_t i = 0; i < smooth.size(); ++i) {
            const Vec3 n = model::normalized(smooth[i]);
            const double facing = std::max(0.0, model::dot(n, frame.eye));
            shades[i] = 0.34 + 0.56 * std::max(0.0, model::dot(n, light)) + 0.10 * std::pow(facing, 8.0);
        }
        for (const auto& triangle : face.triangles) {
            const Vec3& a3 = face.vertices[triangle[0]];
            const Vec3 normal =
                model::normalized(model::cross(face.vertices[triangle[1]] - a3, face.vertices[triangle[2]] - a3));
            if (model::dot(normal, frame.eye) < -1e-6) {
                continue;
            }
            const double s0 = shades[triangle[0]];
            const double s1 = shades[triangle[1]];
            const double s2 = shades[triangle[2]];

            const Vec3 a = screen[triangle[0]];
            const Vec3 b = screen[triangle[1]];
            const Vec3 c = screen[triangle[2]];
            const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (std::abs(area) < 1e-12) {
                continue;
            }
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
            const int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
            const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
            const int y1 = std::min(height - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
            for (int y = y0; y <= y1; ++y) {
                const double py = y + 0.5;
                for (int x = x0; x <= x1; ++x) {
                    const double px = x + 0.5;
                    const double w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                    const double w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                    const double w2 = 1.0 - w0 - w1;
                    if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) {
                        continue;
                    }
                    const float z = static_cast<float>(w0 * a.z + w1 * b.z + w2 * c.z);
                    const std::size_t index = static_cast<std::size_t>(y) * width + x;
                    if (z > frame.depth[index]) {
                        const double shade = w0 * s0 + w1 * s1 + w2 * s2;
                        frame.depth[index] = z;
                        frame.pixels[index] = pack({base.r * shade, base.g * shade, base.b * shade});
                        frame.faces[index] = faceIndex;
                    }
                }
            }
        }
    }

    if (style.edges) {
        const Vec3 size = snapshot.boxMax - snapshot.boxMin;
        const float bias = static_cast<float>(std::sqrt(model::dot(size, size)) * 0.004);
        const std::uint32_t edgeColor = pack(kEdge);
        for (const model::Edge& edge : snapshot.edges) {
            if (edge.faceA == edge.faceB) {
                continue;
            }
            for (std::size_t i = 1; i < edge.points.size(); ++i) {
                const Vec3 p = frame.project(edge.points[i - 1]);
                const Vec3 q = frame.project(edge.points[i]);
                const int steps =
                    std::max(1, static_cast<int>(std::ceil(std::max(std::abs(q.x - p.x), std::abs(q.y - p.y)))));
                for (int s = 0; s <= steps; ++s) {
                    const double t = static_cast<double>(s) / steps;
                    const int x = static_cast<int>(std::floor(p.x + (q.x - p.x) * t));
                    const int y = static_cast<int>(std::floor(p.y + (q.y - p.y) * t));
                    if (x < 0 || y < 0 || x >= width || y >= height) {
                        continue;
                    }
                    const float z = static_cast<float>(p.z + (q.z - p.z) * t);
                    const std::size_t index = static_cast<std::size_t>(y) * width + x;
                    if (z + bias >= frame.depth[index]) {
                        frame.pixels[index] = edgeColor;
                    }
                }
            }
        }
    }
    return frame;
}

} // namespace kalve137::look
