// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kernel/shapes.h"

#include "geometry/exchange.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_DataMap.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace kalve137::kernel {

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
using AncestorMap = NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher>;
using OriginMap = NCollection_DataMap<TopoDS_Shape, Origin, TopTools_ShapeMapHasher>;

struct Shape::Impl {
    TopoDS_Shape shape;
    std::vector<TopoDS_Shape> faces;
    std::vector<TopoDS_Shape> edges;
};

int Shape::faceCount() const
{
    return m_impl ? static_cast<int>(m_impl->faces.size()) : 0;
}

int Shape::edgeCount() const
{
    return m_impl ? static_cast<int>(m_impl->edges.size()) : 0;
}

namespace {

template <typename F>
auto guarded(const char* what, F&& work)
{
    try {
        return work();
    } catch (const Standard_Failure& failure) {
        throw KernelError(std::string("The kernel failed to ") + what + ": " +
                          (failure.what() != nullptr && *failure.what() != '\0' ? failure.what() : "no reason given"));
    }
}

model::Vec3 vec(const gp_XYZ& p)
{
    return {p.X(), p.Y(), p.Z()};
}

gp_Pnt point(model::Vec3 v)
{
    return {v.x, v.y, v.z};
}

gp_Dir direction(model::Vec3 v)
{
    return {v.x, v.y, v.z};
}

Shape wrap(const TopoDS_Shape& topo)
{
    auto impl = std::make_shared<Shape::Impl>();
    impl->shape = topo;
    ShapeMap faces;
    TopExp::MapShapes(topo, TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i) {
        impl->faces.push_back(faces.FindKey(i));
    }
    ShapeMap edges;
    TopExp::MapShapes(topo, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
        if (!BRep_Tool::Degenerated(TopoDS::Edge(edges.FindKey(i)))) {
            impl->edges.push_back(edges.FindKey(i));
        }
    }
    return Shape(std::move(impl));
}

template <typename Op>
Result carry(Op& op, std::initializer_list<const Shape*> inputs, OriginMap& from)
{
    int input = 0;
    for (const Shape* shape : inputs) {
        const std::vector<TopoDS_Shape>& faces = shape->impl().faces;
        for (int face = 0; face < static_cast<int>(faces.size()); ++face) {
            if (op.IsDeleted(faces[face])) {
                continue;
            }
            const NCollection_List<TopoDS_Shape>& modified = op.Modified(faces[face]);
            if (modified.IsEmpty() && !from.IsBound(faces[face])) {
                from.Bind(faces[face], Origin{input, face, -1});
            }
            for (const TopoDS_Shape& made : modified) {
                if (!from.IsBound(made)) {
                    from.Bind(made, Origin{input, face, -1});
                }
            }
        }
        ++input;
    }
    Result result;
    result.shape = wrap(op.Shape());
    for (const TopoDS_Shape& face : result.shape.impl().faces) {
        const Origin* found = from.Seek(face);
        result.origins.push_back(found != nullptr ? *found : Origin{});
    }
    return result;
}

double cross(Point2 a, Point2 b, Point2 c)
{
    return (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
}

Point2 circumcenter(Point2 a, Point2 b, Point2 c)
{
    const double d = 2.0 * (a.u * (b.v - c.v) + b.u * (c.v - a.v) + c.u * (a.v - b.v));
    const double a2 = a.u * a.u + a.v * a.v;
    const double b2 = b.u * b.u + b.v * b.v;
    const double c2 = c.u * c.u + c.v * c.v;
    return {(a2 * (b.v - c.v) + b2 * (c.v - a.v) + c2 * (a.v - b.v)) / d,
            (a2 * (c.u - b.u) + b2 * (a.u - c.u) + c2 * (b.u - a.u)) / d};
}

double turning(const Loop& loop)
{
    std::vector<Point2> points;
    for (const Segment& segment : loop) {
        if (segment.kind == Segment::Kind::Circle) {
            return 1.0;
        }
        points.push_back(segment.a);
        if (segment.kind == Segment::Kind::Arc) {
            points.push_back(segment.m);
        }
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point2 p = points[i];
        const Point2 q = points[(i + 1) % points.size()];
        sum += p.u * q.v - q.u * p.v;
    }
    return sum;
}

struct SegmentMiddle {
    Point2 point;
    Point2 outward;
};

SegmentMiddle middleOf(const Segment& segment, bool counterClockwise)
{
    const double side = counterClockwise ? 1.0 : -1.0;
    switch (segment.kind) {
    case Segment::Kind::Line: {
        const Point2 t{segment.b.u - segment.a.u, segment.b.v - segment.a.v};
        const double length = std::hypot(t.u, t.v);
        return {{(segment.a.u + segment.b.u) / 2.0, (segment.a.v + segment.b.v) / 2.0},
                {side * t.v / length, -side * t.u / length}};
    }
    case Segment::Kind::Arc: {
        const Point2 c = circumcenter(segment.a, segment.m, segment.b);
        const bool left = cross(segment.a, segment.m, segment.b) > 0.0;
        const double start = std::atan2(segment.a.v - c.v, segment.a.u - c.u);
        const double end = std::atan2(segment.b.v - c.v, segment.b.u - c.u);
        double sweep = left ? end - start : start - end;
        while (sweep <= 0.0) {
            sweep += 2.0 * std::numbers::pi;
        }
        const double angle = start + (left ? sweep : -sweep) / 2.0;
        const double radius = std::hypot(segment.a.u - c.u, segment.a.v - c.v);
        const double outside = left == counterClockwise ? 1.0 : -1.0;
        return {{c.u + radius * std::cos(angle), c.v + radius * std::sin(angle)},
                {outside * std::cos(angle), outside * std::sin(angle)}};
    }
    case Segment::Kind::Circle:
        return {{segment.c.u + segment.radius, segment.c.v}, {1.0, 0.0}};
    }
    return {};
}

TopoDS_Shape compound(const std::vector<TopoDS_Shape>& shapes, const std::vector<int>& items)
{
    BRep_Builder builder;
    TopoDS_Compound result;
    builder.MakeCompound(result);
    for (const int index : items) {
        builder.Add(result, shapes.at(index));
    }
    return result;
}

} // namespace

Prism prism(const model::Frame& frame, const Loop& loop, model::Vec3 offset, model::Vec3 vector)
{
    return guarded("extrude a profile", [&] {
        if (loop.empty()) {
            throw KernelError("The kernel got an empty profile");
        }
        model::Frame shifted = frame;
        shifted.origin = frame.origin + offset;
        const auto at = [&](Point2 p) { return point(model::at(shifted, p.u, p.v)); };
        const bool counterClockwise = turning(loop) >= 0.0;
        BRepBuilderAPI_MakeWire wire;
        std::vector<gp_Pnt> middles;
        std::vector<model::Vec3> outwards;
        for (const Segment& segment : loop) {
            const SegmentMiddle middle = middleOf(segment, counterClockwise);
            middles.push_back(at(middle.point));
            outwards.push_back(model::normalized(shifted.x * middle.outward.u + shifted.y * middle.outward.v));
            switch (segment.kind) {
            case Segment::Kind::Line:
                wire.Add(BRepBuilderAPI_MakeEdge(at(segment.a), at(segment.b)).Edge());
                break;
            case Segment::Kind::Arc: {
                const GC_MakeArcOfCircle arc(at(segment.a), at(segment.m), at(segment.b));
                if (!arc.IsDone()) {
                    throw KernelError("The kernel could not make an arc through three points that lie on a line");
                }
                wire.Add(BRepBuilderAPI_MakeEdge(arc.Value()).Edge());
                break;
            }
            case Segment::Kind::Circle: {
                const gp_Circ circle(gp_Ax2(at(segment.c), direction(shifted.normal), direction(shifted.x)),
                                     segment.radius);
                wire.Add(BRepBuilderAPI_MakeEdge(circle).Edge());
                break;
            }
            }
        }
        if (!wire.IsDone()) {
            throw KernelError("The kernel could not join the profile into one closed loop");
        }
        const BRepBuilderAPI_MakeFace makeFace(wire.Wire(), true);
        if (!makeFace.IsDone()) {
            throw KernelError("The kernel could not fill the profile");
        }
        const TopoDS_Face& face = makeFace.Face();
        BRepPrimAPI_MakePrism made(face, gp_Vec(vector.x, vector.y, vector.z));
        made.Build();
        if (!made.IsDone()) {
            throw KernelError("The kernel could not extrude a profile");
        }
        NCollection_DataMap<TopoDS_Shape, PrismFace, TopTools_ShapeMapHasher> origins;
        origins.Bind(made.FirstShape(), {PrismFace::Kind::Start, -1, {}});
        origins.Bind(made.LastShape(), {PrismFace::Kind::End, -1, {}});
        for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
            const BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
            const gp_Pnt mid = curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2.0);
            int nearest = 0;
            for (int i = 1; i < static_cast<int>(middles.size()); ++i) {
                if (mid.SquareDistance(middles[i]) < mid.SquareDistance(middles[nearest])) {
                    nearest = i;
                }
            }
            for (const TopoDS_Shape& side : made.Generated(it.Current())) {
                origins.Bind(side, {PrismFace::Kind::Side, nearest, outwards[nearest]});
            }
        }
        Prism result;
        result.shape = wrap(made.Shape());
        for (const TopoDS_Shape& f : result.shape.impl().faces) {
            const PrismFace* found = origins.Seek(f);
            if (found == nullptr) {
                throw KernelError("The kernel made a prism face that no segment accounts for");
            }
            result.faces.push_back(*found);
        }
        return result;
    });
}

Result join(const Shape& a, const Shape& b)
{
    return guarded("join solids", [&] {
        BRepAlgoAPI_Fuse op(a.impl().shape, b.impl().shape);
        if (!op.IsDone() || op.HasErrors()) {
            throw KernelError("The kernel could not join the solids");
        }
        OriginMap from;
        return carry(op, {&a, &b}, from);
    });
}

Result cut(const Shape& a, const Shape& b)
{
    return guarded("cut solids", [&] {
        BRepAlgoAPI_Cut op(a.impl().shape, b.impl().shape);
        if (!op.IsDone() || op.HasErrors()) {
            throw KernelError("The kernel could not cut the solids");
        }
        OriginMap from;
        return carry(op, {&a, &b}, from);
    });
}

Result round(const Shape& body, const std::vector<int>& edges, double radius)
{
    return guarded("round edges", [&] {
        BRepFilletAPI_MakeFillet op(body.impl().shape);
        for (const int edge : edges) {
            op.Add(radius, TopoDS::Edge(body.impl().edges.at(edge)));
        }
        op.Build();
        if (!op.IsDone()) {
            throw KernelError("The kernel could not round the edges");
        }
        OriginMap from;
        for (const int edge : edges) {
            for (const TopoDS_Shape& made : op.Generated(body.impl().edges[edge])) {
                from.Bind(made, Origin{-1, -1, edge});
            }
        }
        return carry(op, {&body}, from);
    });
}

Inspection inspect(const Shape& shape, Detail detail)
{
    return guarded("measure the body", [&] {
        const bool meshes = detail.meshes;
        Inspection out;
        const TopoDS_Shape& topo = shape.impl().shape;
        Bnd_Box box;
        if (detail.measures) {
            BRepBndLib::AddOptimal(topo, box, false, false);
        } else {
            BRepBndLib::Add(topo, box, false);
        }
        if (!box.IsVoid()) {
            double x0 = 0;
            double y0 = 0;
            double z0 = 0;
            double x1 = 0;
            double y1 = 0;
            double z1 = 0;
            box.Get(x0, y0, z0, x1, y1, z1);
            out.boxMin = {x0, y0, z0};
            out.boxMax = {x1, y1, z1};
        }
        const model::Vec3 size = out.boxMax - out.boxMin;
        const double deflection = std::max(std::sqrt(model::dot(size, size)) * 0.0015, 0.005);
        if (meshes) {
            BRepMesh_IncrementalMesh mesher(topo, deflection, false, 0.3, true);
        }
        if (detail.measures) {
            GProp_GProps volume;
            BRepGProp::VolumeProperties(topo, volume);
            out.volume = volume.Mass();
        }
        ShapeMap solids;
        TopExp::MapShapes(topo, TopAbs_SOLID, solids);
        out.solids = solids.Extent();

        ShapeMap faceMap;
        for (const TopoDS_Shape& face : shape.impl().faces) {
            faceMap.Add(face);
            FaceInfo f;
            const TopoDS_Face& topoFace = TopoDS::Face(face);
            const BRepAdaptor_Surface surface(topoFace);
            switch (surface.GetType()) {
            case GeomAbs_Plane: {
                f.kind = "plane";
                gp_Dir n = surface.Plane().Axis().Direction();
                if (topoFace.Orientation() == TopAbs_REVERSED) {
                    n.Reverse();
                }
                f.normal = vec(n.XYZ());
                break;
            }
            case GeomAbs_Cylinder:
                f.kind = "cylinder";
                f.axis = vec(surface.Cylinder().Axis().Direction().XYZ());
                f.radius = surface.Cylinder().Radius();
                break;
            case GeomAbs_Cone:
                f.kind = "cone";
                break;
            case GeomAbs_Sphere:
                f.kind = "sphere";
                break;
            case GeomAbs_Torus:
                f.kind = "torus";
                break;
            default:
                f.kind = "curved";
                break;
            }
            const int index = static_cast<int>(out.faces.size());
            if (detail.measures || std::binary_search(detail.centroids.begin(), detail.centroids.end(), index)) {
                GProp_GProps props;
                BRepGProp::SurfaceProperties(face, props);
                f.area = props.Mass();
                f.centroid = vec(props.CentreOfMass().XYZ());
            }
            if (meshes) {
                TopLoc_Location location;
                const Handle(Poly_Triangulation) mesh = BRep_Tool::Triangulation(topoFace, location);
                if (!mesh.IsNull()) {
                    const gp_Trsf transform = location.Transformation();
                    for (int i = 1; i <= mesh->NbNodes(); ++i) {
                        f.vertices.push_back(vec(mesh->Node(i).Transformed(transform).XYZ()));
                    }
                    const bool reversed = topoFace.Orientation() == TopAbs_REVERSED;
                    for (int i = 1; i <= mesh->NbTriangles(); ++i) {
                        int a = 0;
                        int b = 0;
                        int c = 0;
                        mesh->Triangle(i).Get(a, b, c);
                        f.triangles.push_back(reversed ? std::array{a - 1, c - 1, b - 1}
                                                       : std::array{a - 1, b - 1, c - 1});
                    }
                }
            }
            out.faces.push_back(std::move(f));
        }

        AncestorMap edgeFaces;
        TopExp::MapShapesAndAncestors(topo, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
        for (const TopoDS_Shape& shapeEdge : shape.impl().edges) {
            const TopoDS_Edge& edge = TopoDS::Edge(shapeEdge);
            EdgeInfo e;
            if (const NCollection_List<TopoDS_Shape>* faces = edgeFaces.Seek(edge)) {
                for (const TopoDS_Shape& face : *faces) {
                    const int index = faceMap.FindIndex(face) - 1;
                    if (e.faceA < 0) {
                        e.faceA = index;
                    } else if (index != e.faceA) {
                        e.faceB = index;
                    }
                }
            }
            if (e.faceB < 0) {
                e.faceB = e.faceA;
            }
            BRepAdaptor_Curve curve(edge);
            e.kind =
                curve.GetType() == GeomAbs_Line ? "line" : (curve.GetType() == GeomAbs_Circle ? "circle" : "curve");
            if (detail.measures) {
                e.length = GCPnts_AbscissaPoint::Length(curve);
            }
            if (meshes) {
                GCPnts_TangentialDeflection points(curve, 0.2, deflection);
                for (int k = 1; k <= points.NbPoints(); ++k) {
                    e.points.push_back(vec(points.Value(k).XYZ()));
                }
            }
            out.edges.push_back(std::move(e));
        }
        return out;
    });
}

std::optional<model::Frame> faceFrame(const Shape& shape, int face)
{
    return guarded("read a face plane", [&]() -> std::optional<model::Frame> {
        const TopoDS_Face& topoFace = TopoDS::Face(shape.impl().faces.at(face));
        const BRepAdaptor_Surface surface(topoFace);
        if (surface.GetType() != GeomAbs_Plane) {
            return std::nullopt;
        }
        const gp_Pln plane = surface.Plane();
        gp_Dir normal = plane.Axis().Direction();
        if (topoFace.Orientation() == TopAbs_REVERSED) {
            normal.Reverse();
        }
        return model::planeFrame(vec(normal.XYZ()), vec(plane.Location().XYZ()));
    });
}

double distance(const Shape& shape, const Pick& a, const Pick& b)
{
    return guarded("measure a distance", [&] {
        const auto& impl = shape.impl();
        BRepExtrema_DistShapeShape measured(compound(a.edges ? impl.edges : impl.faces, a.items),
                                            compound(b.edges ? impl.edges : impl.faces, b.items));
        if (!measured.IsDone()) {
            throw KernelError("The kernel could not measure this distance");
        }
        return measured.Value();
    });
}

void exportStep(const Shape& shape, const std::string& name, const std::filesystem::path& path)
{
    guarded("write STEP", [&] {
        geometry::StepPart part;
        part.name = name;
        part.shape = shape.impl().shape;
        geometry::writeStep({part}, path);
        return 0;
    });
}

std::vector<Shape> importStep(const std::filesystem::path& path)
{
    return guarded("read STEP", [&] {
        std::vector<Shape> shapes;
        for (const geometry::StepPart& part : geometry::readStepParts(path)) {
            shapes.push_back(wrap(part.shape));
        }
        return shapes;
    });
}

} // namespace kalve137::kernel
