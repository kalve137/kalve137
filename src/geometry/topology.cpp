// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/topology.h"

#include "geometry/modeling.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BVH_BoxSet.hxx>
#include <BVH_Tools.hxx>
#include <BVH_Traverse.hxx>
#include <Bnd_Box.hxx>
#include <IntCurvesFace_Intersector.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <utility>

namespace kalve137::geometry {

namespace {

constexpr double kTangentDegrees = 1.0;
constexpr double kMatchTolerance = 1e-4;
constexpr int kEdgeSamples = 5;
constexpr int kFaceSteps = 4;
constexpr double kViewClearance = 1e-3;

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

using FaceBoxes = BVH_BoxSet<double, 3, int>;

class RayHit : public BVH_Traverse<double, 3, FaceBoxes, bool> {
public:
    RayHit(const gp_Lin& ray, std::function<bool(int)> hits)
        : m_origin(ray.Location().X(), ray.Location().Y(), ray.Location().Z()),
          m_direction(ray.Direction().X(), ray.Direction().Y(), ray.Direction().Z()), m_hits(std::move(hits))
    {}

    bool RejectNode(const BVH_Vec3d& low, const BVH_Vec3d& high, bool&) const override
    {
        double enter = 0.0;
        double leave = 0.0;
        return !BVH_Tools<double, 3>::RayBoxIntersection(m_origin, m_direction, low, high, enter, leave);
    }

    bool Accept(const int index, const bool&) override
    {
        m_found = m_hits(myBVHSet->Element(index));
        return m_found;
    }

    bool Stop() const override { return m_found; }

    bool found() const { return m_found; }

private:
    BVH_Vec3d m_origin;
    BVH_Vec3d m_direction;
    std::function<bool(int)> m_hits;
    bool m_found = false;
};

std::vector<gp_Pnt> faceSamples(const TopoDS_Face& face)
{
    const BRepAdaptor_Surface surface(face);
    double uMin = 0.0;
    double uMax = 0.0;
    double vMin = 0.0;
    double vMax = 0.0;
    BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
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
    std::vector<TopoDS_Face> faces;
    std::vector<std::unique_ptr<IntCurvesFace_Intersector>> intersectors;
    opencascade::handle<FaceBoxes> boxes = new FaceBoxes();
};

ViewBlockers::ViewBlockers(const std::vector<TopoDS_Shape>& shapes) : m_faces(std::make_unique<Faces>())
{
    for (const TopoDS_Shape& shape : shapes) {
        for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
            Bnd_Box box;
            BRepBndLib::Add(it.Current(), box);
            if (box.IsVoid()) {
                continue;
            }
            box.Enlarge(Precision::Confusion());
            const gp_Pnt low = box.CornerMin();
            const gp_Pnt high = box.CornerMax();
            m_faces->boxes->Add(
                static_cast<int>(m_faces->faces.size()),
                BVH_Box<double, 3>(BVH_Vec3d(low.X(), low.Y(), low.Z()), BVH_Vec3d(high.X(), high.Y(), high.Z())));
            m_faces->faces.push_back(TopoDS::Face(it.Current()));
        }
    }
    m_faces->intersectors.resize(m_faces->faces.size());
    m_faces->boxes->Build();
}

ViewBlockers::~ViewBlockers() = default;

bool ViewBlockers::blocks(const gp_Pnt& point, const gp_Dir& towardViewer) const
{
    const gp_Lin ray(point, towardViewer);
    RayHit hit(ray, [&](int index) {
        try {
            std::unique_ptr<IntCurvesFace_Intersector>& intersector = m_faces->intersectors[index];
            if (!intersector) {
                intersector =
                    std::make_unique<IntCurvesFace_Intersector>(m_faces->faces[index], Precision::Confusion());
            }
            intersector->Perform(ray, kViewClearance, Precision::Infinite());
            return intersector->IsDone() && intersector->NbPnt() > 0;
        } catch (const Standard_Failure&) {
            return false;
        }
    });
    hit.SetBVHSet(m_faces->boxes.get());
    hit.Select();
    return hit.found();
}

} // namespace kalve137::geometry
