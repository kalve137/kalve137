// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/naming.h"

#include "geometry/modeling.h"
#include "geometry/topology.h"
#include "sketch/profiles.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <ranges>
#include <variant>

namespace kalve137::core {

namespace {

constexpr double kParallel = 1e-6;
constexpr double kOnPlane = 1e-6;
constexpr std::size_t kKeptPoints = 256;
constexpr std::size_t kBoxesPerBody = 2;
constexpr std::size_t kSpareBoxes = 64;

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

bool samePlace(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    if (a.IsEqual(b)) {
        return true;
    }
    if (a.TShape() != b.TShape() || a.Orientation() != b.Orientation()) {
        return false;
    }
    const gp_Trsf& first = a.Location().Transformation();
    const gp_Trsf& second = b.Location().Transformation();
    for (int row = 1; row <= 3; ++row) {
        for (int column = 1; column <= 4; ++column) {
            if (first.Value(row, column) != second.Value(row, column)) {
                return false;
            }
        }
    }
    return true;
}

TopoDS_Face faceAt(const TopoDS_Shape& shape, int index)
{
    TopExp_Explorer it(shape, TopAbs_FACE);
    for (int i = 0; i < index && it.More(); ++i) {
        it.Next();
    }
    return TopoDS::Face(it.Current());
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

bool holdsAnyOf(const geometry::FaceNames& names, const TopoDS_Shape& face, const std::vector<std::string>& wanted)
{
    return std::ranges::any_of(wanted, [&](const std::string& name) { return hasName(names, face, name); });
}

bool facesMatch(const geometry::FaceNames& names, const NCollection_List<TopoDS_Shape>& faces,
                const std::vector<std::string>& wanted)
{
    if (faces.IsEmpty()) {
        return false;
    }
    for (const TopoDS_Shape& face : faces) {
        if (!holdsAnyOf(names, face, wanted)) {
            return false;
        }
    }
    for (const std::string& name : wanted) {
        bool held = false;
        for (const TopoDS_Shape& face : faces) {
            held = held || hasName(names, face, name);
        }
        if (!held) {
            return false;
        }
    }
    return true;
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

std::optional<int> faceIndexOf(const TopoDS_Shape& shape, const TopoDS_Face& face)
{
    int index = 0;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next(), ++index) {
        if (it.Current().IsEqual(face)) {
            return index;
        }
    }
    return std::nullopt;
}

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

bool namesFeature(const Body& body, FeatureId id)
{
    for (TopExp_Explorer it(body.shape, TopAbs_FACE); it.More(); it.Next()) {
        const std::vector<std::string>* names = body.faceNames.find(it.Current());
        if (names != nullptr &&
            std::ranges::any_of(*names, [id](const std::string& name) { return featureOfName(name) == id; })) {
            return true;
        }
    }
    return false;
}

std::vector<BodyKey> bodiesOf(const Model& model, const Feature& feature)
{
    const std::vector<BodyKey>* listed = nullptr;
    if (const auto* move = std::get_if<MoveFeature>(&feature.data); move != nullptr && !move->copy) {
        listed = &move->bodies;
    } else if (const auto* appearance = std::get_if<AppearanceFeature>(&feature.data)) {
        listed = &appearance->bodies;
    }
    std::vector<BodyKey> keys;
    for (const Body& body : model.bodies) {
        if (body.id == feature.id || (listed != nullptr && std::ranges::find(*listed, body.key()) != listed->end()) ||
            namesFeature(body, feature.id)) {
            keys.push_back(body.key());
        }
    }
    return keys;
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

TopoRef vertexRef(const Body& body, const TopoDS_Vertex& vertex)
{
    TopoRef ref{BRep_Tool::Pnt(vertex), {}};
    AncestorMap vertexFaces;
    TopExp::MapShapesAndUniqueAncestors(body.shape, TopAbs_VERTEX, TopAbs_FACE, vertexFaces);
    if (const NCollection_List<TopoDS_Shape>* faces = vertexFaces.Seek(vertex)) {
        ref.names = body.faceNames.namesAround(*faces);
    }
    return ref;
}

void FaceLookup::clear()
{
    m_found.clear();
    m_boxes.Clear();
}

const Bnd_Box& FaceLookup::boxOf(const TopoDS_Shape& shape)
{
    if (const Bnd_Box* box = m_boxes.Seek(shape)) {
        return *box;
    }
    return *m_boxes.Bound(shape, geometry::geometryBox(shape));
}

std::optional<FaceHit> FaceLookup::atPoint(const std::vector<Body>& bodies, const gp_Pnt& point)
{
    const auto kept = std::ranges::find_if(m_found, [&](const Found& old) { return old.point.IsEqual(point, 0.0); });
    const Found* last = kept == m_found.end() ? nullptr : &*kept;
    if (static_cast<std::size_t>(m_boxes.Extent()) > kBoxesPerBody * bodies.size() + kSpareBoxes) {
        m_boxes.Clear();
    }
    Found found{point, {}, std::nullopt, 0};
    for (std::size_t i = 0; i < bodies.size() && !found.hit; ++i) {
        const TopoDS_Shape& shape = bodies[i].shape;
        found.shapes.push_back(shape);
        if (last != nullptr && i < last->shapes.size() && samePlace(last->shapes[i], shape)) {
            if (last->hit && last->hit->body == i) {
                found.hit = FaceHit{i, faceAt(shape, last->faceIndex)};
                found.faceIndex = last->faceIndex;
            }
            continue;
        }
        if (!geometry::mayHold(boxOf(shape), point, kRefTolerance)) {
            continue;
        }
        if (auto face = geometry::findFace(shape, point, kRefTolerance)) {
            const std::optional<int> index = faceIndexOf(shape, *face);
            if (!index) {
                if (last != nullptr) {
                    m_found.erase(kept);
                }
                return FaceHit{i, *face};
            }
            found.hit = FaceHit{i, *face};
            found.faceIndex = *index;
        }
    }
    if (last != nullptr) {
        *kept = std::move(found);
        return kept->hit;
    }
    if (m_found.size() >= kKeptPoints) {
        m_found.erase(m_found.begin());
    }
    m_found.push_back(std::move(found));
    return m_found.back().hit;
}

std::optional<FaceHit> resolveFace(const std::vector<Body>& bodies, const TopoRef& ref, FaceLookup* lookup)
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
    if (lookup != nullptr) {
        return lookup->atPoint(bodies, ref.point);
    }
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (!geometry::mayHold(geometry::geometryBox(bodies[i].shape), ref.point, kRefTolerance)) {
            continue;
        }
        if (auto face = geometry::findFace(bodies[i].shape, ref.point, kRefTolerance)) {
            return FaceHit{i, *face};
        }
    }
    return std::nullopt;
}

ShapeAncestors::ShapeAncestors(const std::vector<Body>& bodies)
    : m_bodies(bodies), m_edgeFaces(bodies.size()), m_vertexFaces(bodies.size())
{}

const AncestorMap& ShapeAncestors::edgeFaces(std::size_t body)
{
    std::optional<AncestorMap>& map = m_edgeFaces.at(body);
    if (!map) {
        map.emplace();
        TopExp::MapShapesAndUniqueAncestors(m_bodies[body].shape, TopAbs_EDGE, TopAbs_FACE, *map);
    }
    return *map;
}

const AncestorMap& ShapeAncestors::vertexFaces(std::size_t body)
{
    std::optional<AncestorMap>& map = m_vertexFaces.at(body);
    if (!map) {
        map.emplace();
        TopExp::MapShapesAndUniqueAncestors(m_bodies[body].shape, TopAbs_VERTEX, TopAbs_FACE, *map);
    }
    return *map;
}

std::optional<EdgeHit> resolveEdge(const std::vector<Body>& bodies, const TopoRef& ref)
{
    ShapeAncestors ancestors(bodies);
    return resolveEdge(ancestors, ref);
}

std::optional<EdgeHit> resolveEdge(ShapeAncestors& ancestors, const TopoRef& ref)
{
    const std::vector<Body>& bodies = ancestors.bodies();
    if (ref.names.size() == 2) {
        std::optional<EdgeHit> best;
        double bestDistance = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            const geometry::FaceNames& names = bodies[i].faceNames;
            const AncestorMap& edgeFaces = ancestors.edgeFaces(i);
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

std::optional<VertexHit> resolveVertex(const std::vector<Body>& bodies, const TopoRef& ref)
{
    ShapeAncestors ancestors(bodies);
    return resolveVertex(ancestors, ref);
}

std::optional<VertexHit> resolveVertex(ShapeAncestors& ancestors, const TopoRef& ref)
{
    const std::vector<Body>& bodies = ancestors.bodies();
    std::optional<VertexHit> named;
    double namedDistance = std::numeric_limits<double>::max();
    std::optional<VertexHit> near;
    double nearDistance = kRefTolerance;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const AncestorMap& vertexFaces = ancestors.vertexFaces(i);
        for (int v = 1; v <= vertexFaces.Extent(); ++v) {
            const TopoDS_Vertex& vertex = TopoDS::Vertex(vertexFaces.FindKey(v));
            const double distance = ref.point.Distance(BRep_Tool::Pnt(vertex));
            if (!ref.names.empty() && distance < namedDistance &&
                facesMatch(bodies[i].faceNames, vertexFaces(v), ref.names)) {
                named = VertexHit{i, vertex};
                namedDistance = distance;
            }
            if (distance <= nearDistance) {
                near = VertexHit{i, vertex};
                nearDistance = distance;
            }
        }
    }
    return named ? named : near;
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
