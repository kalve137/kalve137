// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/naming.h"

#include "geometry/modeling.h"
#include "geometry/topology.h"
#include "sketch/profiles.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>

namespace kalve137::core {

namespace {

constexpr double kParallel = 1e-6;
constexpr double kOnPlane = 1e-6;

using AncestorMap = NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher>;

double distanceTo(const gp_Pnt& point, const TopoDS_Shape& shape)
{
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(point);
    BRepExtrema_DistShapeShape distance(probe, shape);
    return distance.IsDone() ? distance.Value() : std::numeric_limits<double>::max();
}

bool hasName(const geometry::FaceNames& names, const TopoDS_Shape& face, const std::string& name)
{
    const std::vector<std::string>* list = names.find(face);
    return list != nullptr && std::ranges::find(*list, name) != list->end();
}

int sharedNames(const geometry::FaceNames& names, const TopoDS_Shape& face, const std::vector<std::string>& wanted)
{
    const std::vector<std::string>* list = names.find(face);
    if (list == nullptr) {
        return 0;
    }
    return static_cast<int>(std::ranges::count_if(
        wanted, [&](const std::string& name) { return std::ranges::find(*list, name) != list->end(); }));
}

class CurveFinder {
public:
    explicit CurveFinder(const SketchState& state)
    {
        for (const auto& [id, curve] : state.solved.curves()) {
            if (curve.construction) {
                continue;
            }
            if (const std::optional<TopoDS_Edge> edge = sketch::makeEdge(state.solved, id, state.plane)) {
                m_edges.emplace(id, *edge);
            }
        }
    }

    std::optional<std::pair<sketch::Id, double>> nearest(const gp_Pnt& point) const
    {
        std::optional<std::pair<sketch::Id, double>> best;
        for (const auto& [id, edge] : m_edges) {
            const double distance = distanceTo(point, edge);
            if (!best || distance < best->second) {
                best = {id, distance};
            }
        }
        return best;
    }

private:
    std::map<sketch::Id, TopoDS_Edge> m_edges;
};

std::string profileSuffix(const SketchState& state, const std::vector<TopoDS_Face>& profiles, const gp_Pnt& onPlane)
{
    const std::optional<std::size_t> index =
        sketch::profileAt(profiles, state.plane, sketch::toSketch(state.plane, onPlane));
    return index ? "/p" + std::to_string(*index) : std::string();
}

std::optional<gp_Pln> planeOf(const TopoDS_Face& face)
{
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) {
        return std::nullopt;
    }
    return surface.Plane();
}

} // namespace

std::string namePrefix(FeatureId id)
{
    return "F" + std::to_string(id);
}

FeatureId featureOfName(const std::string& name)
{
    if (name.size() < 2 || name.front() != 'F') {
        return 0;
    }
    FeatureId id = 0;
    const char* first = name.data() + 1;
    const auto [end, error] = std::from_chars(first, name.data() + name.size(), id);
    return error == std::errc() && end != first ? id : 0;
}

FeatureId featureOfFace(const Body& body, const TopoDS_Face& face)
{
    return featureOfName(body.faceNames.primary(face));
}

FeatureId featureOfEdge(const Body& body, const TopoDS_Edge& edge)
{
    FeatureId latest = 0;
    for (const std::string& name : body.faceNames.edgeNames(body.shape, edge)) {
        latest = std::max(latest, featureOfName(name));
    }
    return latest;
}

TopoRef faceRef(const Body& body, const TopoDS_Face& face, const gp_Pnt& point)
{
    TopoRef ref{point, {}};
    if (const std::vector<std::string>* names = body.faceNames.find(face)) {
        ref.names = *names;
    }
    return ref;
}

TopoRef edgeRef(const Body& body, const TopoDS_Edge& edge, const gp_Pnt& point)
{
    return {point, body.faceNames.edgeNames(body.shape, edge)};
}

std::optional<FaceHit> resolveFace(const std::vector<Body>& bodies, const TopoRef& ref)
{
    std::optional<FaceHit> best;
    int bestShared = 0;
    double bestDistance = std::numeric_limits<double>::max();
    if (!ref.names.empty()) {
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            for (TopExp_Explorer it(bodies[i].shape, TopAbs_FACE); it.More(); it.Next()) {
                const int shared = sharedNames(bodies[i].faceNames, it.Current(), ref.names);
                if (shared == 0 || shared < bestShared) {
                    continue;
                }
                const double distance = distanceTo(ref.point, it.Current());
                if (shared > bestShared || distance < bestDistance) {
                    best = FaceHit{i, TopoDS::Face(it.Current())};
                    bestShared = shared;
                    bestDistance = distance;
                }
            }
        }
        if (best) {
            return best;
        }
    }
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (auto face = geometry::findFace(bodies[i].shape, ref.point, kRefTolerance)) {
            return FaceHit{i, *face};
        }
    }
    return std::nullopt;
}

std::optional<EdgeHit> resolveEdge(const std::vector<Body>& bodies, const TopoRef& ref)
{
    if (ref.names.size() == 2) {
        std::optional<EdgeHit> best;
        double bestDistance = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            const geometry::FaceNames& names = bodies[i].faceNames;
            AncestorMap edgeFaces;
            TopExp::MapShapesAndUniqueAncestors(bodies[i].shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
            for (int e = 1; e <= edgeFaces.Extent(); ++e) {
                const NCollection_List<TopoDS_Shape>& faces = edgeFaces(e);
                if (faces.IsEmpty()) {
                    continue;
                }
                const TopoDS_Shape& first = faces.First();
                const TopoDS_Shape& last = faces.Last();
                const bool matches = (hasName(names, first, ref.names[0]) && hasName(names, last, ref.names[1])) ||
                                     (hasName(names, first, ref.names[1]) && hasName(names, last, ref.names[0]));
                if (!matches) {
                    continue;
                }
                const double distance = distanceTo(ref.point, edgeFaces.FindKey(e));
                if (distance < bestDistance) {
                    best = EdgeHit{i, TopoDS::Edge(edgeFaces.FindKey(e))};
                    bestDistance = distance;
                }
            }
        }
        if (best) {
            return best;
        }
    }
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (auto edge = geometry::findEdge(bodies[i].shape, ref.point, kRefTolerance)) {
            return EdgeHit{i, *edge};
        }
    }
    return std::nullopt;
}

std::optional<gp_Ax1> holeAxis(const std::vector<Body>& bodies, const TopoRef& ref)
{
    const std::optional<FaceHit> hit = resolveFace(bodies, ref);
    if (!hit) {
        return std::nullopt;
    }
    gp_Pnt at = ref.point;
    BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(ref.point).Vertex(), hit->face);
    if (distance.IsDone() && distance.NbSolution() > 0) {
        at = distance.PointOnShape2(1);
    }
    const std::optional<gp_Dir> outward = geometry::faceNormal(hit->face, at);
    return outward ? std::optional<gp_Ax1>(gp_Ax1(at, outward->Reversed())) : std::nullopt;
}

geometry::FaceNames nameExtrusion(const TopoDS_Shape& tool, FeatureId id, const SketchState& state,
                                  const std::vector<TopoDS_Face>& profiles, const gp_Vec& from, const gp_Vec& to)
{
    const std::string prefix = namePrefix(id);
    const gp_Dir normal = state.plane.Direction();
    const gp_Pnt origin = state.plane.Location();
    const double start = from.Dot(gp_Vec(normal));
    const double end = to.Dot(gp_Vec(normal));
    const CurveFinder curves(state);
    geometry::FaceNames names;
    for (TopExp_Explorer it(tool, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face& face = TopoDS::Face(it.Current());
        const gp_Pnt point = geometry::facePoint(face);
        const double height = gp_Vec(origin, point).Dot(gp_Vec(normal));
        const gp_Pnt onPlane = point.Translated(gp_Vec(normal) * -height);
        const std::optional<gp_Pln> plane = planeOf(face);
        if (plane && std::abs(std::abs(plane->Axis().Direction().Dot(normal)) - 1.0) < kParallel) {
            const char* cap = std::abs(height - start) <= std::abs(height - end) ? "/start" : "/end";
            names.add(face, prefix + cap + profileSuffix(state, profiles, onPlane));
            continue;
        }
        if (const auto curve = curves.nearest(onPlane)) {
            names.add(face, prefix + "/side/c" + std::to_string(curve->first));
        }
    }
    names.nameRest(tool, prefix);
    return names;
}

geometry::FaceNames nameRevolution(const TopoDS_Shape& tool, FeatureId id, const SketchState& state,
                                   const std::vector<TopoDS_Face>& profiles, const gp_Ax1& axis)
{
    const std::string prefix = namePrefix(id);
    const gp_Vec along(axis.Direction());
    const gp_Vec normal(state.plane.Direction());
    gp_Vec across = along.Crossed(normal);
    if (across.Magnitude() < kParallel) {
        geometry::FaceNames names;
        names.nameRest(tool, prefix);
        return names;
    }
    across.Normalize();
    const CurveFinder curves(state);
    geometry::FaceNames names;
    for (TopExp_Explorer it(tool, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face& face = TopoDS::Face(it.Current());
        const gp_Pnt point = geometry::facePoint(face);
        const gp_Vec offset(axis.Location(), point);
        const double height = offset.Dot(along);
        const double radius = (offset - along * height).Magnitude();
        const gp_Pnt base = axis.Location().Translated(along * height);
        std::optional<std::pair<sketch::Id, double>> best;
        gp_Pnt source;
        for (const double side : {1.0, -1.0}) {
            const gp_Pnt candidate = base.Translated(across * (side * radius));
            const auto curve = curves.nearest(candidate);
            if (curve && (!best || curve->second < best->second)) {
                best = curve;
                source = candidate;
            }
        }
        const std::optional<gp_Pln> plane = planeOf(face);
        const bool holdsAxis = plane && std::abs(plane->Axis().Direction().Dot(axis.Direction())) < kParallel &&
                               plane->Distance(axis.Location()) < kOnPlane;
        if (holdsAxis) {
            const bool onSketch = gp_Pln(state.plane).Distance(point) < kOnPlane;
            names.add(face, prefix + (onSketch ? "/start" : "/end") + profileSuffix(state, profiles, source));
            continue;
        }
        if (best) {
            names.add(face, prefix + "/rev/c" + std::to_string(best->first));
        }
    }
    names.nameRest(tool, prefix);
    return names;
}

geometry::FaceNames nameByIndex(const TopoDS_Shape& shape, const std::string& prefix)
{
    geometry::FaceNames names;
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i) {
        names.add(faces(i), prefix + "/f" + std::to_string(i - 1));
    }
    return names;
}

} // namespace kalve137::core
