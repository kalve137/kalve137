// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/box_select.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace kalve137::render {

namespace {

using camera::ScreenPoint;

double cross(ScreenPoint origin, ScreenPoint a, ScreenPoint b)
{
    return ((a.x - origin.x) * (b.y - origin.y)) - ((a.y - origin.y) * (b.x - origin.x));
}

bool segmentsCross(ScreenPoint a, ScreenPoint b, ScreenPoint c, ScreenPoint d)
{
    const double abc = cross(a, b, c);
    const double abd = cross(a, b, d);
    const double cda = cross(c, d, a);
    const double cdb = cross(c, d, b);
    return ((abc <= 0.0 && abd >= 0.0) || (abc >= 0.0 && abd <= 0.0)) &&
           ((cda <= 0.0 && cdb >= 0.0) || (cda >= 0.0 && cdb <= 0.0));
}

std::array<ScreenPoint, 4> cornersOf(const ScreenRect& rect)
{
    return {ScreenPoint{rect.left, rect.top}, ScreenPoint{rect.right, rect.top}, ScreenPoint{rect.right, rect.bottom},
            ScreenPoint{rect.left, rect.bottom}};
}

bool segmentTouches(ScreenPoint a, ScreenPoint b, const ScreenRect& rect)
{
    if (rect.contains(a) || rect.contains(b)) {
        return true;
    }
    if (std::max(a.x, b.x) < rect.left || std::min(a.x, b.x) > rect.right || std::max(a.y, b.y) < rect.top ||
        std::min(a.y, b.y) > rect.bottom) {
        return false;
    }
    const std::array<ScreenPoint, 4> corners = cornersOf(rect);
    for (std::size_t i = 0; i < corners.size(); ++i) {
        if (segmentsCross(a, b, corners[i], corners[(i + 1) % corners.size()])) {
            return true;
        }
    }
    return false;
}

bool triangleHolds(const std::array<ScreenPoint, 3>& triangle, ScreenPoint point)
{
    const double first = cross(triangle[0], triangle[1], point);
    const double second = cross(triangle[1], triangle[2], point);
    const double third = cross(triangle[2], triangle[0], point);
    return (first >= 0.0 && second >= 0.0 && third >= 0.0) || (first <= 0.0 && second <= 0.0 && third <= 0.0);
}

bool triangleTouches(const std::array<ScreenPoint, 3>& triangle, const ScreenRect& rect)
{
    for (std::size_t i = 0; i < triangle.size(); ++i) {
        if (segmentTouches(triangle[i], triangle[(i + 1) % triangle.size()], rect)) {
            return true;
        }
    }
    return triangleHolds(triangle, {rect.left, rect.top});
}

enum class State : std::uint8_t { Unseen, In, Out };

void collect(const std::vector<State>& states, const snapshot::Body& body, PickKind kind,
             std::vector<PickedItem>& items)
{
    for (std::size_t index = 0; index < states.size(); ++index) {
        if (states[index] == State::In) {
            items.push_back(
                {.feature = body.feature, .part = body.part, .kind = kind, .index = static_cast<std::uint32_t>(index)});
        }
    }
}

void boxFaces(const snapshot::Body& body, const std::vector<ScreenPoint>& screen, const ScreenRect& rect, bool crossing,
              std::vector<PickedItem>& items)
{
    const snapshot::Mesh& mesh = *body.mesh;
    std::vector<State> states(mesh.faceCount, State::Unseen);
    for (std::size_t t = 0; t < mesh.triangles.size() && t < mesh.faces.size(); ++t) {
        const std::uint32_t face = mesh.faces[t];
        if (face >= states.size()) {
            continue;
        }
        State& state = states[face];
        const snapshot::Triangle& corners = mesh.triangles[t];
        const std::array<ScreenPoint, 3> triangle = {screen[corners[0]], screen[corners[1]], screen[corners[2]]};
        if (crossing) {
            if (state != State::In && triangleTouches(triangle, rect)) {
                state = State::In;
            }
        } else if (state != State::Out) {
            const bool inside = std::ranges::all_of(triangle, [&](ScreenPoint point) { return rect.contains(point); });
            state = inside ? State::In : State::Out;
        }
    }
    collect(states, body, PickKind::Face, items);
}

void boxEdges(const snapshot::Body& body, const camera::Camera& camera, const camera::Viewport& viewport,
              const ScreenRect& rect, bool crossing, std::vector<PickedItem>& items)
{
    const snapshot::Mesh& mesh = *body.mesh;
    std::vector<State> states(mesh.edgeCount, State::Unseen);
    std::vector<ScreenPoint> screen;
    for (const snapshot::Edge& edge : mesh.edges) {
        if (edge.id >= states.size() || edge.points.empty()) {
            continue;
        }
        screen.clear();
        for (const snapshot::Point& point : edge.points) {
            screen.push_back(camera.project({point[0], point[1], point[2]}, viewport));
        }
        bool in = false;
        if (crossing) {
            in = screen.size() == 1 ? rect.contains(screen.front()) : false;
            for (std::size_t i = 1; i < screen.size() && !in; ++i) {
                in = segmentTouches(screen[i - 1], screen[i], rect);
            }
        } else {
            in = std::ranges::all_of(screen, [&](ScreenPoint point) { return rect.contains(point); });
        }
        states[edge.id] = in ? State::In : State::Out;
    }
    collect(states, body, PickKind::Edge, items);
}

void boxPoints(const snapshot::Body& body, const camera::Camera& camera, const camera::Viewport& viewport,
               const ScreenRect& rect, std::vector<PickedItem>& items)
{
    const snapshot::Mesh& mesh = *body.mesh;
    std::vector<State> states(mesh.pointCount, State::Unseen);
    for (const snapshot::Vertex& vertex : mesh.points) {
        if (vertex.id < states.size()) {
            const ScreenPoint point = camera.project({vertex.point[0], vertex.point[1], vertex.point[2]}, viewport);
            states[vertex.id] = rect.contains(point) ? State::In : State::Out;
        }
    }
    collect(states, body, PickKind::Point, items);
}

} // namespace

ScreenRect ScreenRect::spanning(camera::ScreenPoint from, camera::ScreenPoint to)
{
    return {.left = std::min(from.x, to.x),
            .top = std::min(from.y, to.y),
            .right = std::max(from.x, to.x),
            .bottom = std::max(from.y, to.y)};
}

bool ScreenRect::contains(camera::ScreenPoint point) const
{
    return point.x >= left && point.x <= right && point.y >= top && point.y <= bottom;
}

bool isCrossingBox(camera::ScreenPoint from, camera::ScreenPoint to)
{
    return to.x < from.x;
}

std::vector<PickedItem> boxCandidates(const std::vector<std::shared_ptr<const snapshot::Body>>& bodies,
                                      const camera::Camera& camera, const camera::Viewport& viewport,
                                      const ScreenRect& rect, bool crossing, const PickKinds& kinds)
{
    camera::Camera shown = camera;
    shown.setAspect(viewport.aspect());
    std::vector<PickedItem> items;
    std::vector<ScreenPoint> screen;
    for (const auto& body : bodies) {
        if (body == nullptr || body->mesh == nullptr) {
            continue;
        }
        const snapshot::Mesh& mesh = *body->mesh;
        if (kinds.faces) {
            screen.resize(mesh.vertices.size());
            for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
                const snapshot::Point& vertex = mesh.vertices[i];
                screen[i] = shown.project({vertex[0], vertex[1], vertex[2]}, viewport);
            }
            boxFaces(*body, screen, rect, crossing, items);
        }
        if (kinds.edges) {
            boxEdges(*body, shown, viewport, rect, crossing, items);
        }
        if (kinds.points) {
            boxPoints(*body, shown, viewport, rect, items);
        }
    }
    return items;
}

} // namespace kalve137::render
