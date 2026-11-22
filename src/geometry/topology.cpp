// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/topology.h"

#include "geometry/modeling.h"
#include "pick/ray_scene.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>

namespace kalve137::geometry {

namespace {

constexpr double kTangentDegrees = 1.0;
constexpr double kMatchTolerance = 1e-4;
constexpr int kEdgeSamples = 5;
constexpr int kFaceSteps = 4;
constexpr int kPointSteps = 8;
constexpr double kViewClearance = 1e-3;
constexpr double kStepPerClearance = 0.01;
constexpr double kMarginPerDeflection = 2.0;

std::optional<gp_Vec> tangentAt(const TopoDS_Edge& edge, const TopoDS_Vertex& vertex)
{
    const BRepAdaptor_Curve curve(edge);
    gp_Pnt point;
    gp_Vec tangent;
    curve.D1(BRep_Tool::Parameter(vertex, edge), point, tangent);
    if (tangent.Magnitude() < 1e-12) {
        return std::nullopt;
    }
    return tangent;
}

std::optional<gp_Dir> normalAt(const TopoDS_Face& face, const gp_Pnt2d& uv)
{
    const BRepAdaptor_Surface surface(face);
    gp_Pnt point;
    gp_Vec du;
    gp_Vec dv;
    surface.D1(uv.X(), uv.Y(), point, du, dv);
    gp_Vec normal = du.Crossed(dv);
    if (normal.Magnitude() < 1e-12) {
        return std::nullopt;
    }
    if (face.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }
    return gp_Dir(normal);
}

std::optional<gp_Pnt> probePoint(const TopoDS_Shape& shape)
{
    switch (shape.ShapeType()) {
    case TopAbs_VERTEX:
        return BRep_Tool::Pnt(TopoDS::Vertex(shape));
    case TopAbs_EDGE:
        return edgeMidpoint(TopoDS::Edge(shape));
    case TopAbs_FACE:
        return facePoint(TopoDS::Face(shape));
    case TopAbs_COMPOUND:
    case TopAbs_COMPSOLID:
    case TopAbs_SOLID:
    case TopAbs_SHELL:
    case TopAbs_WIRE:
    case TopAbs_SHAPE:
        return std::nullopt;
    }
    return std::nullopt;
}

template <typename Op>
std::optional<TopoDS_Edge> sourceIn(Op& op, const std::vector<TopoDS_Edge>& edges, const gp_Pnt& point)
{
    op.Build();
    if (!op.IsDone()) {
        return std::nullopt;
    }
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(point);
    for (const TopoDS_Edge& edge : edges) {
        for (const TopoDS_Shape& made : op.Generated(edge)) {
            BRepExtrema_DistShapeShape distance(probe, made);
            if (distance.IsDone() && distance.Value() <= kMatchTolerance) {
                return edge;
            }
        }
    }
    return std::nullopt;
}

bool inTriangle(const gp_XY& p, const gp_XY& a, const gp_XY& b, const gp_XY& c)
{
    const double ab = (b - a) ^ (p - a);
    const double bc = (c - b) ^ (p - b);
    const double ca = (a - c) ^ (p - c);
    return (ab >= 0.0 && bc >= 0.0 && ca >= 0.0) || (ab <= 0.0 && bc <= 0.0 && ca <= 0.0);
}

std::optional<std::vector<gp_Pnt>> meshSamples(const TopoDS_Face& face, double uMin, double uMax, double vMin,
                                               double vMax)
{
    TopLoc_Location location;
    const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
    if (triangulation.IsNull() || !triangulation->HasUVNodes() || uMax <= uMin || vMax <= vMin) {
        return std::nullopt;
    }
    const double du = (uMax - uMin) / kPointSteps;
    const double dv = (vMax - vMin) / kPointSteps;
    std::array<std::array<bool, kPointSteps>, kPointSteps> inside{};
    for (int t = 1; t <= triangulation->NbTriangles(); ++t) {
        int a = 0;
        int b = 0;
        int c = 0;
        triangulation->Triangle(t).Get(a, b, c);
        const gp_XY pa = triangulation->UVNode(a).XY();
        const gp_XY pb = triangulation->UVNode(b).XY();
        const gp_XY pc = triangulation->UVNode(c).XY();
        const int iFirst = std::max(1, static_cast<int>(std::ceil((std::min({pa.X(), pb.X(), pc.X()}) - uMin) / du)));
        const int iLast =
            std::min(kPointSteps - 1, static_cast<int>(std::floor((std::max({pa.X(), pb.X(), pc.X()}) - uMin) / du)));
        const int jFirst = std::max(1, static_cast<int>(std::ceil((std::min({pa.Y(), pb.Y(), pc.Y()}) - vMin) / dv)));
        const int jLast =
            std::min(kPointSteps - 1, static_cast<int>(std::floor((std::max({pa.Y(), pb.Y(), pc.Y()}) - vMin) / dv)));
        for (int i = iFirst; i <= iLast; ++i) {
            for (int j = jFirst; j <= jLast; ++j) {
                if (!inside[i][j] && inTriangle(gp_XY(uMin + du * i, vMin + dv * j), pa, pb, pc)) {
                    inside[i][j] = true;
                }
            }
        }
    }
    const BRepAdaptor_Surface surface(face);
    const auto at = [&](int i, int j) { return surface.Value(uMin + du * i, vMin + dv * j); };
    std::optional<gp_Pnt> center;
    int bestOffset = std::numeric_limits<int>::max();
    for (int i = 1; i < kPointSteps; ++i) {
        for (int j = 1; j < kPointSteps; ++j) {
            const int offset = std::abs(i - kPointSteps / 2) + std::abs(j - kPointSteps / 2);
            if (inside[i][j] && offset < bestOffset) {
                center = at(i, j);
                bestOffset = offset;
            }
        }
    }
    std::vector<gp_Pnt> points{center.value_or(surface.Value((uMin + uMax) / 2.0, (vMin + vMax) / 2.0))};
    constexpr int kStride = kPointSteps / kFaceSteps;
    for (int i = kStride; i < kPointSteps; i += kStride) {
        for (int j = kStride; j < kPointSteps; j += kStride) {
            if (inside[i][j]) {
                points.push_back(at(i, j));
            }
        }
    }
    return points;
}

std::vector<gp_Pnt> faceSamples(const TopoDS_Face& face)
{
    const BRepAdaptor_Surface surface(face);
    double uMin = 0.0;
    double uMax = 0.0;
    double vMin = 0.0;
    double vMax = 0.0;
    BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
    if (std::optional<std::vector<gp_Pnt>> points = meshSamples(face, uMin, uMax, vMin, vMax)) {
        return *std::move(points);
    }
    std::vector<gp_Pnt> points{facePoint(face)};
    for (int i = 1; i < kFaceSteps; ++i) {
        for (int j = 1; j < kFaceSteps; ++j) {
            const gp_Pnt2d uv(uMin + (uMax - uMin) * i / kFaceSteps, vMin + (vMax - vMin) * j / kFaceSteps);
            BRepClass_FaceClassifier classifier(face, uv, Precision::Confusion());
            if (classifier.State() == TopAbs_IN) {
                points.push_back(surface.Value(uv.X(), uv.Y()));
            }
        }
    }
    return points;
}

} // namespace

std::optional<gp_Dir> faceNormal(const TopoDS_Face& face, const gp_Pnt& around)
{
    BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(around), face);
    if (!distance.IsDone() || distance.NbSolution() == 0 || distance.SupportTypeShape2(1) != BRepExtrema_IsInFace) {
        return std::nullopt;
    }
    double u = 0.0;
    double v = 0.0;
    distance.ParOnFaceS2(1, u, v);
    return normalAt(face, gp_Pnt2d(u, v));
}

std::optional<gp_Dir> edgeBisector(const TopoDS_Shape& shape, const TopoDS_Edge& edge)
{
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> facesOf;
    TopExp::MapShapesAndUniqueAncestors(shape, TopAbs_EDGE, TopAbs_FACE, facesOf);
    if (!facesOf.Contains(edge) || facesOf.FindFromKey(edge).Size() != 2) {
        return std::nullopt;
    }
    gp_Vec sum(0.0, 0.0, 0.0);
    for (const TopoDS_Shape& shapeFace : facesOf.FindFromKey(edge)) {
        const TopoDS_Face& face = TopoDS::Face(shapeFace);
        const BRepAdaptor_Curve2d onFace(edge, face);
        const gp_Pnt2d uv = onFace.Value((onFace.FirstParameter() + onFace.LastParameter()) / 2.0);
        const std::optional<gp_Dir> normal = normalAt(face, uv);
        if (!normal) {
            return std::nullopt;
        }
        sum += gp_Vec(*normal);
    }
    if (sum.Magnitude() < 1e-6) {
        return std::nullopt;
    }
    return gp_Dir(sum.Reversed());
}

std::vector<TopoDS_Edge> tangentChain(const TopoDS_Shape& shape, const TopoDS_Edge& seed)
{
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> edgesAt;
    TopExp::MapShapesAndUniqueAncestors(shape, TopAbs_VERTEX, TopAbs_EDGE, edgesAt);
    const double minCos = std::cos(kTangentDegrees * std::numbers::pi / 180.0);

    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> chain;
    chain.Add(seed);
    for (int i = 1; i <= chain.Extent(); ++i) {
        const TopoDS_Edge edge = TopoDS::Edge(chain(i));
        TopoDS_Vertex first;
        TopoDS_Vertex last;
        TopExp::Vertices(edge, first, last);
        for (const TopoDS_Vertex& vertex : {first, last}) {
            if (vertex.IsNull() || !edgesAt.Contains(vertex)) {
                continue;
            }
            const std::optional<gp_Vec> along = tangentAt(edge, vertex);
            if (!along) {
                continue;
            }
            for (const TopoDS_Shape& neighbor : edgesAt.FindFromKey(vertex)) {
                const TopoDS_Edge& next = TopoDS::Edge(neighbor);
                if (chain.Contains(next) || BRep_Tool::Degenerated(next)) {
                    continue;
                }
                const std::optional<gp_Vec> other = tangentAt(next, vertex);
                if (other && std::abs(along->Normalized().Dot(other->Normalized())) >= minCos) {
                    chain.Add(next);
                }
            }
        }
    }

    std::vector<TopoDS_Edge> result;
    for (int i = 1; i <= chain.Extent(); ++i) {
        result.push_back(TopoDS::Edge(chain(i)));
    }
    return result;
}

std::optional<gp_Ax1> lineAlong(const TopoDS_Edge& edge)
{
    const BRepAdaptor_Curve curve(edge);
    if (curve.GetType() != GeomAbs_Line) {
        return std::nullopt;
    }
    const gp_Pnt start = curve.Value(curve.FirstParameter());
    const gp_Pnt end = curve.Value(curve.LastParameter());
    if (start.Distance(end) < kMatchTolerance) {
        return std::nullopt;
    }
    return gp_Ax1(start, gp_Dir(gp_Vec(start, end)));
}

std::optional<gp_Ax1> circleAxis(const TopoDS_Edge& edge)
{
    const BRepAdaptor_Curve curve(edge);
    if (curve.GetType() != GeomAbs_Circle) {
        return std::nullopt;
    }
    return curve.Circle().Axis();
}

std::optional<gp_Ax1> revolutionAxis(const TopoDS_Face& face)
{
    const BRepAdaptor_Surface surface(face);
    switch (surface.GetType()) {
    case GeomAbs_Cylinder:
        return surface.Cylinder().Axis();
    case GeomAbs_Cone:
        return surface.Cone().Axis();
    default:
        return std::nullopt;
    }
}

std::optional<TopoDS_Face> flatFaceAlong(const TopoDS_Shape& shape, const TopoDS_Edge& edge)
{
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> facesOf;
    TopExp::MapShapesAndUniqueAncestors(shape, TopAbs_EDGE, TopAbs_FACE, facesOf);
    if (!facesOf.Contains(edge)) {
        return std::nullopt;
    }
    for (const TopoDS_Shape& candidate : facesOf.FindFromKey(edge)) {
        if (BRepAdaptor_Surface(TopoDS::Face(candidate)).GetType() == GeomAbs_Plane) {
            return TopoDS::Face(candidate);
        }
    }
    return std::nullopt;
}

std::optional<TopoDS_Edge> blendSource(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges, double size,
                                       Blend blend, const TopoDS_Shape& picked)
{
    const std::optional<gp_Pnt> point = picked.IsNull() ? std::nullopt : probePoint(picked);
    if (!point || edges.empty() || size <= 0.0) {
        return std::nullopt;
    }
    try {
        switch (blend) {
        case Blend::Fillet: {
            BRepFilletAPI_MakeFillet op(shape);
            for (const TopoDS_Edge& edge : edges) {
                op.Add(size, edge);
            }
            return sourceIn(op, edges, *point);
        }
        case Blend::Chamfer: {
            BRepFilletAPI_MakeChamfer op(shape);
            for (const TopoDS_Edge& edge : edges) {
                op.Add(size, edge);
            }
            return sourceIn(op, edges, *point);
        }
        }
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<gp_Pnt> samplePoints(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) {
        return {};
    }
    switch (shape.ShapeType()) {
    case TopAbs_VERTEX:
        return {BRep_Tool::Pnt(TopoDS::Vertex(shape))};
    case TopAbs_EDGE: {
        const BRepAdaptor_Curve curve(TopoDS::Edge(shape));
        std::vector<gp_Pnt> points;
        for (int i = 1; i < kEdgeSamples + 1; ++i) {
            const double t = static_cast<double>(i) / (kEdgeSamples + 1);
            points.push_back(
                curve.Value(curve.FirstParameter() + t * (curve.LastParameter() - curve.FirstParameter())));
        }
        return points;
    }
    case TopAbs_FACE:
        return faceSamples(TopoDS::Face(shape));
    case TopAbs_COMPOUND:
    case TopAbs_COMPSOLID:
    case TopAbs_SOLID:
    case TopAbs_SHELL:
    case TopAbs_WIRE:
    case TopAbs_SHAPE:
        return {};
    }
    return {};
}

struct ViewBlockers::Faces {
    std::shared_ptr<const pick::RayScene> scene;
    std::vector<double> margins;
};

ViewBlockers::ViewBlockers(const std::vector<const snapshot::Mesh*>& meshes)
    : ViewBlockers(std::make_shared<const pick::RayScene>(meshes), meshes)
{}

ViewBlockers::ViewBlockers(std::shared_ptr<const pick::RayScene> rays, const std::vector<const snapshot::Mesh*>& meshes)
    : m_faces(std::make_unique<Faces>())
{
    for (const snapshot::Mesh* mesh : meshes) {
        for (std::uint32_t face = 0; face < mesh->faceCount; ++face) {
            const double deflection = face < mesh->faceDeflections.size() ? mesh->faceDeflections[face] : 0.0;
            m_faces->margins.push_back(kViewClearance + kMarginPerDeflection * deflection);
        }
    }
    m_faces->scene = std::move(rays);
}

ViewBlockers::~ViewBlockers() = default;

bool ViewBlockers::blocks(const gp_Pnt& point, const gp_Dir& towardViewer) const
{
    const pick::Point origin{point.X(), point.Y(), point.Z()};
    const pick::Point direction{towardViewer.X(), towardViewer.Y(), towardViewer.Z()};
    if (m_faces->scene == nullptr) {
        return false;
    }
    double from = 0.0;
    while (const std::optional<pick::Hit> hit = m_faces->scene->first(origin, direction, from)) {
        if (hit->distance > m_faces->margins[hit->face]) {
            return true;
        }
        from = std::max(hit->distance, from) + kViewClearance * kStepPerClearance;
    }
    return false;
}

} // namespace kalve137::geometry
