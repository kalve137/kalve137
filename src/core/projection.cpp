// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/projection.h"

#include "core/naming.h"
#include "log/log.h"
#include "sketch/profiles.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <gp_Circ.hxx>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace kalve137::core {

namespace {

constexpr double kParallelAngle = 1e-9;

bool finite(sketch::Vec2 at)
{
    return std::isfinite(at.x) && std::isfinite(at.y);
}

bool finite(const gp_Pnt& at)
{
    return std::isfinite(at.X()) && std::isfinite(at.Y()) && std::isfinite(at.Z());
}

bool linksProjected(const sketch::Sketch& sketch, const Projection& link)
{
    if (link.curve != 0) {
        const sketch::Curve* curve = sketch.curve(link.curve);
        return curve != nullptr && curve->projected && std::ranges::all_of(curve->points, [&](sketch::Id id) {
                   const sketch::Point* point = sketch.point(id);
                   return point != nullptr && point->projected;
               });
    }
    const sketch::Point* point = sketch.point(link.point);
    return point != nullptr && point->projected;
}

std::vector<sketch::Id> itemsOf(const sketch::Sketch& sketch, const Projection& link)
{
    std::vector<sketch::Id> items{link.curve != 0 ? link.curve : link.point};
    if (const sketch::Curve* curve = sketch.curve(link.curve)) {
        items.insert(items.end(), curve->points.begin(), curve->points.end());
    }
    return items;
}

std::set<sketch::Id> coveredBy(const sketch::Sketch& sketch, const std::vector<Projection>& links)
{
    std::set<sketch::Id> covered;
    for (const Projection& link : links) {
        const std::vector<sketch::Id> items = itemsOf(sketch, link);
        covered.insert(items.begin(), items.end());
    }
    return covered;
}

void release(sketch::Sketch& sketch, sketch::Id id)
{
    if (sketch::Curve* curve = sketch.curve(id)) {
        curve->projected = false;
        curve->fixed = false;
    } else if (sketch::Point* point = sketch.point(id)) {
        point->projected = false;
        point->fixed = false;
    }
}

sketch::Id addProjectedPoint(sketch::Sketch& sketch, sketch::Vec2 at)
{
    const sketch::Id id = sketch.addPoint(at, true);
    sketch.point(id)->projected = true;
    return id;
}

} // namespace

std::optional<ProjectedEdge> projectEdge(const TopoDS_Edge& edge, const gp_Ax3& plane)
{
    const BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter();
    const double last = curve.LastParameter();
    if (!std::isfinite(first) || !std::isfinite(last)) {
        return std::nullopt;
    }
    sketch::Vec2 start = sketch::toSketch(plane, curve.Value(first));
    sketch::Vec2 end = sketch::toSketch(plane, curve.Value(last));
    ProjectedEdge result;
    switch (curve.GetType()) {
    case GeomAbs_Line:
        if (sketch::distance(start, end) < Precision::Confusion()) {
            result = {ProjectedShape::Point, {start}, 0.0};
        } else {
            result = {ProjectedShape::Line, {start, end}, 0.0};
        }
        break;
    case GeomAbs_Circle: {
        const gp_Circ circle = curve.Circle();
        const gp_Dir& axis = circle.Axis().Direction();
        if (!axis.IsParallel(plane.Direction(), kParallelAngle)) {
            return std::nullopt;
        }
        const sketch::Vec2 center = sketch::toSketch(plane, circle.Location());
        if (curve.Value(first).Distance(curve.Value(last)) < Precision::Confusion()) {
            result = {ProjectedShape::Circle, {center}, circle.Radius()};
            break;
        }
        if (axis.Dot(plane.Direction()) < 0.0) {
            std::swap(start, end);
        }
        result = {ProjectedShape::Arc, {center, start, end}, circle.Radius()};
        break;
    }
    default:
        return std::nullopt;
    }
    if (!std::ranges::all_of(result.points, [](sketch::Vec2 at) { return finite(at); }) ||
        !std::isfinite(result.radius)) {
        return std::nullopt;
    }
    return result;
}

std::optional<sketch::Vec2> projectVertex(const TopoDS_Vertex& vertex, const gp_Ax3& plane)
{
    const sketch::Vec2 at = sketch::toSketch(plane, BRep_Tool::Pnt(vertex));
    return finite(at) ? std::optional(at) : std::nullopt;
}

Projection addProjectedEdge(SketchFeature& feature, const ProjectedEdge& edge, TopoRef ref)
{
    sketch::Sketch& s = feature.sketch;
    Projection link{ProjectionKind::Edge, 0, 0, std::move(ref)};
    switch (edge.shape) {
    case ProjectedShape::Point:
        link.point = addProjectedPoint(s, edge.points[0]);
        break;
    case ProjectedShape::Line:
        link.curve = s.addLine(addProjectedPoint(s, edge.points[0]), addProjectedPoint(s, edge.points[1]));
        break;
    case ProjectedShape::Circle:
        link.curve = s.addCircle(addProjectedPoint(s, edge.points[0]), edge.radius);
        break;
    case ProjectedShape::Arc:
        link.curve = s.addArc(addProjectedPoint(s, edge.points[0]), addProjectedPoint(s, edge.points[1]),
                              addProjectedPoint(s, edge.points[2]));
        break;
    }
    if (sketch::Curve* curve = s.curve(link.curve)) {
        curve->fixed = true;
        curve->projected = true;
    }
    feature.projections.push_back(link);
    return link;
}

Projection addProjectedVertex(SketchFeature& feature, sketch::Vec2 at, TopoRef ref)
{
    Projection link{ProjectionKind::Vertex, 0, addProjectedPoint(feature.sketch, at), std::move(ref)};
    feature.projections.push_back(link);
    return link;
}

std::optional<std::string> validateProjections(const SketchFeature& feature)
{
    std::set<sketch::Id> linked;
    for (const Projection& link : feature.projections) {
        if (!finite(link.ref.point)) {
            return "a projection has no finite point";
        }
        if ((link.curve != 0) == (link.point != 0)) {
            return "a projection must link one curve or one point";
        }
        if (link.kind == ProjectionKind::Vertex && link.curve != 0) {
            return "a projected vertex links a curve";
        }
        if (!linksProjected(feature.sketch, link)) {
            return "a projection links a missing or unprojected item";
        }
        if (!linked.insert(link.curve != 0 ? link.curve : link.point).second) {
            return "two projections link the same item";
        }
    }
    const std::set<sketch::Id> covered = coveredBy(feature.sketch, feature.projections);
    for (const auto& [id, point] : feature.sketch.points()) {
        if (point.projected && !covered.contains(id)) {
            return "a projected point has no projection";
        }
    }
    for (const auto& [id, curve] : feature.sketch.curves()) {
        if (curve.projected && !covered.contains(id)) {
            return "a projected curve has no projection";
        }
    }
    return std::nullopt;
}

void dropBrokenProjections(SketchFeature& feature)
{
    sketch::Sketch& s = feature.sketch;
    std::erase_if(feature.projections, [&](const Projection& link) { return !linksProjected(s, link); });
    const std::set<sketch::Id> covered = coveredBy(s, feature.projections);
    std::vector<sketch::Id> released;
    std::vector<sketch::Id> unused;
    for (const auto& [id, curve] : s.curves()) {
        if (curve.projected && !covered.contains(id)) {
            released.push_back(id);
        }
    }
    for (const auto& [id, point] : s.points()) {
        if (point.projected && !covered.contains(id)) {
            (s.curvesUsing(id).empty() ? unused : released).push_back(id);
        }
    }
    for (const sketch::Id id : released) {
        release(s, id);
    }
    for (const sketch::Id id : unused) {
        s.remove(id);
    }
}

bool placeProjection(sketch::Sketch& sketch, const Projection& link, const ProjectedEdge& edge)
{
    if (link.curve == 0) {
        sketch::Point* point = sketch.point(link.point);
        if (point == nullptr || edge.shape != ProjectedShape::Point) {
            return false;
        }
        point->pos = edge.points[0];
        return true;
    }
    sketch::Curve* curve = sketch.curve(link.curve);
    if (curve == nullptr) {
        return false;
    }
    std::vector<sketch::Vec2> at = edge.points;
    switch (curve->kind) {
    case sketch::CurveKind::Line: {
        if (edge.shape != ProjectedShape::Line || curve->points.size() != 2) {
            return false;
        }
        const sketch::Vec2 a = sketch.point(curve->points[0])->pos;
        const sketch::Vec2 b = sketch.point(curve->points[1])->pos;
        if (sketch::distance(at[0], b) + sketch::distance(at[1], a) <
            sketch::distance(at[0], a) + sketch::distance(at[1], b)) {
            std::swap(at[0], at[1]);
        }
        break;
    }
    case sketch::CurveKind::Circle:
        if (edge.shape != ProjectedShape::Circle || curve->points.size() != 1) {
            return false;
        }
        break;
    case sketch::CurveKind::Arc:
        if (edge.shape != ProjectedShape::Arc || curve->points.size() != 3) {
            return false;
        }
        break;
    }
    for (std::size_t i = 0; i < at.size(); ++i) {
        sketch.point(curve->points[i])->pos = at[i];
    }
    curve->radius = edge.radius;
    return true;
}

namespace {

std::optional<ProjectedEdge> follow(ShapeAncestors& ancestors, const Projection& link, const gp_Ax3& plane)
{
    if (link.kind == ProjectionKind::Vertex) {
        const std::optional<VertexHit> hit = resolveVertex(ancestors, link.ref);
        const std::optional<sketch::Vec2> at = hit ? projectVertex(hit->vertex, plane) : std::nullopt;
        return at ? std::optional(ProjectedEdge{ProjectedShape::Point, {*at}, 0.0}) : std::nullopt;
    }
    const std::optional<EdgeHit> hit = resolveEdge(ancestors, link.ref);
    return hit ? projectEdge(hit->edge, plane) : std::nullopt;
}

} // namespace

std::vector<sketch::Id> followProjections(sketch::Sketch& sketch, const std::vector<Projection>& links,
                                          const std::vector<Body>& bodies, const gp_Ax3& plane)
{
    std::vector<sketch::Id> broken;
    ShapeAncestors ancestors(bodies);
    for (const Projection& link : links) {
        std::optional<ProjectedEdge> projected;
        try {
            projected = follow(ancestors, link, plane);
        } catch (const Standard_Failure& failure) {
            K137_DEBUG(Core, "Projection lost its model edge: {}", failure.what() != nullptr ? failure.what() : "");
        }
        if (!projected || !placeProjection(sketch, link, *projected)) {
            broken.push_back(link.curve != 0 ? link.curve : link.point);
        }
    }
    return broken;
}

std::string brokenProjectionsMessage(std::size_t broken)
{
    if (broken == 1) {
        return "1 projected curve lost its model edge";
    }
    return std::to_string(broken) + " projected curves lost their model edge";
}

Model modelBefore(const Document& document, const Model& current, FeatureId sketch)
{
    const int index = document.indexOf(sketch);
    if (index < 0 || static_cast<std::size_t>(index) + 1 >= document.marker()) {
        return current;
    }
    Document before = document;
    before.setMarker(static_cast<std::size_t>(index));
    return recompute(before);
}

} // namespace kalve137::core
