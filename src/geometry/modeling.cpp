// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/modeling.h"

#include "geometry/shape_history.h"
#include "geometry/shapes.h"
#include "geometry/topology.h"

#include "log/log.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Geom2d_Curve.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_List.hxx>
#include <Precision.hxx>
#include <ShapeBuild_ReShape.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace kalve137::geometry {

namespace {

constexpr double kOffsetTolerance = 1e-4;
constexpr double kMinDistance = 1e-6;
constexpr double kHoleEntryMargin = 0.01;
constexpr double kShellWallSlack = 2.0;
constexpr double kTangentAngle = std::numbers::pi / 180.0;

TopoDS_Shape unify(const TopoDS_Shape& shape, BRepTools_History* history = nullptr)
{
    ShapeUpgrade_UnifySameDomain unifier(shape, true, true, true);
    unifier.Build();
    record(history, unifier.History());
    return unifier.Shape();
}

[[noreturn]] void fail(const char* what)
{
    throw std::runtime_error(what);
}

template <typename Op>
TopoDS_Shape guarded(const char* what, Op&& op)
{
    try {
        TopoDS_Shape result = op();
        if (result.IsNull()) {
            K137_DEBUG(Geometry, "{}: the result is empty", what);
            fail(what);
        }
        return result;
    } catch (const Standard_Failure& failure) {
        K137_DEBUG(Geometry, "{}: OCCT failure {}", what, failure.what() != nullptr ? failure.what() : "");
        fail(what);
    }
}

std::optional<std::pair<gp_Pnt, double>> roundCenter(const BRepAdaptor_Surface& surface, const gp_Pnt& point)
{
    const GeomAbs_SurfaceType type = surface.GetType();
    if (type == GeomAbs_Cylinder) {
        const gp_Cylinder cylinder = surface.Cylinder();
        const gp_Ax1 axis = cylinder.Axis();
        const double along = gp_Vec(axis.Location(), point).Dot(gp_Vec(axis.Direction()));
        return std::pair{axis.Location().Translated(gp_Vec(axis.Direction()) * along), cylinder.Radius()};
    }
    if (type == GeomAbs_Sphere) {
        const gp_Sphere sphere = surface.Sphere();
        return std::pair{sphere.Location(), sphere.Radius()};
    }
    if (type == GeomAbs_Torus) {
        const gp_Torus torus = surface.Torus();
        const gp_Vec axis(torus.Axis().Direction());
        gp_Vec radial(torus.Location(), point);
        radial -= axis * radial.Dot(axis);
        if (radial.Magnitude() < Precision::Confusion()) {
            return std::nullopt;
        }
        radial.Normalize();
        return std::pair{torus.Location().Translated(radial * torus.MajorRadius()), torus.MinorRadius()};
    }
    return std::nullopt;
}

bool hasEveryWall(const TopoDS_Shape& source, const TopoDS_Shape& result, const std::vector<TopoDS_Face>& openFaces,
                  double thickness, bool inside)
{
    const double depth = inside ? -thickness / 2.0 : thickness / 2.0;
    for (TopExp_Explorer it(source, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face& face = TopoDS::Face(it.Current());
        if (std::ranges::any_of(openFaces, [&](const TopoDS_Face& open) { return open.IsSame(face); })) {
            continue;
        }
        const gp_Pnt point = facePoint(face);
        const std::optional<gp_Dir> outward = faceNormal(face, point);
        if (!outward) {
            continue;
        }
        BRepClass3d_SolidClassifier classifier(result, point.Translated(gp_Vec(*outward) * depth),
                                               Precision::Confusion());
        if (classifier.State() != TopAbs_IN) {
            return false;
        }
    }
    return true;
}

std::optional<double> smallestConvexRound(const TopoDS_Shape& shape)
{
    std::optional<double> smallest;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face& face = TopoDS::Face(it.Current());
        const BRepAdaptor_Surface surface(face);
        const double u = (surface.FirstUParameter() + surface.LastUParameter()) / 2.0;
        const double v = (surface.FirstVParameter() + surface.LastVParameter()) / 2.0;
        gp_Pnt point;
        gp_Vec du;
        gp_Vec dv;
        surface.D1(u, v, point, du, dv);
        gp_Vec outward = du.Crossed(dv);
        const std::optional<std::pair<gp_Pnt, double>> round = roundCenter(surface, point);
        if (!round || outward.Magnitude() < Precision::Confusion()) {
            continue;
        }
        if (face.Orientation() == TopAbs_REVERSED) {
            outward.Reverse();
        }
        if (outward.Dot(gp_Vec(round->first, point)) > 0.0 && (!smallest || round->second < *smallest)) {
            smallest = round->second;
        }
    }
    return smallest;
}

std::optional<gp_Vec> normalAlong(const TopoDS_Edge& edge, const TopoDS_Face& face)
{
    double first = 0.0;
    double last = 0.0;
    const Handle(Geom2d_Curve) curve = BRep_Tool::CurveOnSurface(edge, face, first, last);
    if (curve.IsNull()) {
        return std::nullopt;
    }
    const gp_Pnt2d uv = curve->Value((first + last) / 2.0);
    gp_Pnt point;
    gp_Vec du;
    gp_Vec dv;
    BRepAdaptor_Surface(face).D1(uv.X(), uv.Y(), point, du, dv);
    const gp_Vec normal = du.Crossed(dv);
    if (normal.Magnitude() < Precision::Confusion()) {
        return std::nullopt;
    }
    return normal;
}

bool runsIntoRound(const TopoDS_Shape& shape, const std::vector<TopoDS_Face>& faces)
{
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> facesOf;
    TopExp::MapShapesAndUniqueAncestors(shape, TopAbs_EDGE, TopAbs_FACE, facesOf);
    for (const TopoDS_Face& face : faces) {
        for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
            const TopoDS_Edge& edge = TopoDS::Edge(it.Current());
            if (!facesOf.Contains(edge)) {
                continue;
            }
            const std::optional<gp_Vec> own = normalAlong(edge, face);
            const gp_Pnt middle = edgeMidpoint(edge);
            for (const TopoDS_Shape& other : facesOf.FindFromKey(edge)) {
                if (other.IsSame(face)) {
                    continue;
                }
                const TopoDS_Face& neighbor = TopoDS::Face(other);
                const std::optional<gp_Vec> next = normalAlong(edge, neighbor);
                if (own && next && own->IsParallel(*next, kTangentAngle) &&
                    roundCenter(BRepAdaptor_Surface(neighbor), middle)) {
                    return true;
                }
            }
        }
    }
    return false;
}

TopoDS_Shape fuseAll(const std::vector<TopoDS_Shape>& shapes)
{
    TopoDS_Shape result = shapes.front();
    for (std::size_t i = 1; i < shapes.size(); ++i) {
        result = fuse(result, shapes[i]);
    }
    return result;
}

std::optional<TopoDS_Shape> draftSides(const TopoDS_Shape& prism, const gp_Dir& pull, const gp_Pln& neutral,
                                       double angle)
{
    BRepOffsetAPI_DraftAngle draft(prism);
    for (TopExp_Explorer it(prism, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face& face = TopoDS::Face(it.Current());
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() == GeomAbs_Plane && surface.Plane().Axis().Direction().IsParallel(pull, kTangentAngle)) {
            continue;
        }
        draft.Add(face, pull, angle, neutral);
        if (!draft.AddDone()) {
            return std::nullopt;
        }
    }
    draft.Build();
    if (!draft.IsDone() || draft.Shape().IsNull()) {
        return std::nullopt;
    }
    return draft.Shape();
}

bool isSoundSolid(const TopoDS_Shape& shape)
{
    if (!BRepCheck_Analyzer(shape).IsValid()) {
        return false;
    }
    BRepAlgoAPI_Check check(shape, true, true);
    return check.IsValid();
}

TopoDS_Shape draftedPrism(const TopoDS_Face& face, const gp_Vec& vector, double taperRadians)
{
    const TopoDS_Shape straight = BRepPrimAPI_MakePrism(face, vector, true).Shape();
    const double straightVolume = std::abs(volume(straight));
    const gp_Dir pull(vector);
    const gp_Pln neutral(facePoint(face), pull);
    for (const double sign : {1.0, -1.0}) {
        const std::optional<TopoDS_Shape> drafted = draftSides(straight, pull, neutral, sign * taperRadians);
        if (!drafted || !isSoundSolid(*drafted)) {
            continue;
        }
        const double draftedVolume = std::abs(volume(*drafted));
        if ((draftedVolume < straightVolume) == (taperRadians > 0.0)) {
            return *drafted;
        }
    }
    fail("The taper is too steep for this profile");
}

TopoDS_Shape taperedExtrude(const std::vector<TopoDS_Face>& faces, const gp_Vec& from, const gp_Vec& to,
                            double taperRadians)
{
    std::vector<TopoDS_Shape> solids;
    for (const TopoDS_Face& face : faces) {
        for (const gp_Vec* end : {&from, &to}) {
            if (end->Magnitude() > kMinDistance) {
                solids.push_back(draftedPrism(face, *end, taperRadians));
            }
        }
    }
    return fuseAll(solids);
}

} // namespace

TopoDS_Shape extrude(const std::vector<TopoDS_Face>& faces, const gp_Vec& from, const gp_Vec& to, double taperRadians)
{
    if (faces.empty()) {
        fail("Nothing to extrude");
    }
    const gp_Vec vector = to - from;
    if (vector.Magnitude() < 1e-9) {
        fail("Extrude distance is zero");
    }
    if (std::abs(taperRadians) >= std::numbers::pi / 2.0) {
        fail("The taper must be less than 90 degrees");
    }
    if (std::abs(taperRadians) > kTangentAngle / 100.0) {
        if (from.Magnitude() > kMinDistance && from.Dot(to) > 0.0) {
            fail("A tapered extrude starts on the sketch plane");
        }
        return guarded("Extrude failed", [&] { return taperedExtrude(faces, from, to, taperRadians); });
    }
    return guarded("Extrude failed", [&] {
        std::vector<TopoDS_Shape> solids;
        for (const TopoDS_Face& face : faces) {
            TopoDS_Shape base = face;
            if (from.Magnitude() > 0.0) {
                gp_Trsf shift;
                shift.SetTranslation(from);
                base = BRepBuilderAPI_Transform(face, shift, true).Shape();
            }
            BRepPrimAPI_MakePrism prism(base, vector, true);
            if (!prism.IsDone()) {
                fail("Extrude failed");
            }
            solids.push_back(prism.Shape());
        }
        return fuseAll(solids);
    });
}

TopoDS_Shape revolve(const std::vector<TopoDS_Face>& faces, const gp_Ax1& axis, double angleRadians)
{
    if (faces.empty()) {
        fail("Nothing to revolve");
    }
    if (angleRadians <= 1e-9) {
        fail("Revolve angle is zero");
    }
    return guarded("Revolve failed", [&] {
        std::vector<TopoDS_Shape> solids;
        for (const TopoDS_Face& face : faces) {
            BRepPrimAPI_MakeRevol revol(face, axis, angleRadians, true);
            if (!revol.IsDone()) {
                fail("Revolve failed");
            }
            solids.push_back(revol.Shape());
        }
        return fuseAll(solids);
    });
}

TopoDS_Shape fuse(const TopoDS_Shape& a, const TopoDS_Shape& b, BRepTools_History* history)
{
    return guarded("Join failed", [&] {
        BRepAlgoAPI_Fuse op(a, b);
        if (!op.IsDone()) {
            fail("Join failed");
        }
        record(history, op.History());
        return unify(op.Shape(), history);
    });
}

TopoDS_Shape pattern(const TopoDS_Shape& shape, const std::vector<gp_Trsf>& placements, BRepTools_History* history)
{
    if (placements.empty()) {
        fail("A pattern needs at least two copies");
    }
    return guarded("Pattern failed", [&] {
        NCollection_List<TopoDS_Shape> arguments;
        arguments.Append(shape);
        NCollection_List<TopoDS_Shape> tools;
        BRepTools_History copies;
        if (history != nullptr) {
            for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
                copies.AddModified(it.Current(), it.Current());
            }
        }
        for (const gp_Trsf& placement : placements) {
            BRepBuilderAPI_Transform transform(shape, placement, true);
            tools.Append(transform.Shape());
            if (history == nullptr) {
                continue;
            }
            for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
                copies.AddModified(it.Current(), transform.ModifiedShape(it.Current()));
            }
        }
        BRepAlgoAPI_Fuse op;
        op.SetArguments(arguments);
        op.SetTools(tools);
        op.Build();
        if (!op.IsDone()) {
            fail("Pattern failed");
        }
        if (history != nullptr) {
            history->Merge(copies);
        }
        record(history, op.History());
        return unify(op.Shape(), history);
    });
}

TopoDS_Shape common(const TopoDS_Shape& a, const TopoDS_Shape& b, BRepTools_History* history)
{
    return guarded("Intersect failed", [&] {
        BRepAlgoAPI_Common op(a, b);
        if (!op.IsDone()) {
            fail("Intersect failed");
        }
        record(history, op.History());
        return unify(op.Shape(), history);
    });
}

bool overlaps(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    Bnd_Box boxA;
    Bnd_Box boxB;
    BRepBndLib::Add(a, boxA);
    BRepBndLib::Add(b, boxB);
    if (boxA.IsOut(boxB)) {
        return false;
    }
    try {
        BRepAlgoAPI_Common op(a, b);
        return op.IsDone() && volume(op.Shape()) > 1e-6;
    } catch (const Standard_Failure&) {
        return false;
    }
}

bool isEmpty(const TopoDS_Shape& shape)
{
    return shape.IsNull() || !TopExp_Explorer(shape, TopAbs_SOLID).More();
}

TopoDS_Shape fillet(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges, double radius,
                    BRepTools_History* history)
{
    return fillet(shape, edges, std::vector<double>(edges.size(), radius), history);
}

TopoDS_Shape fillet(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges, const std::vector<double>& radii,
                    BRepTools_History* history)
{
    if (edges.empty()) {
        fail("No edges to fillet");
    }
    if (radii.size() != edges.size()) {
        fail("Every fillet edge needs a radius");
    }
    if (std::ranges::any_of(radii, [](double radius) { return !(radius > 0.0); })) {
        fail("Fillet radius must be positive");
    }
    return guarded("Fillet failed, the radius may be too large for the selected edges", [&] {
        BRepFilletAPI_MakeFillet op(shape);
        for (std::size_t i = 0; i < edges.size(); ++i) {
            op.Add(radii[i], edges[i]);
        }
        op.Build();
        if (!op.IsDone()) {
            fail("Fillet failed, the radius may be too large for the selected edges");
        }
        record(history, shape, op);
        return op.Shape();
    });
}

TopoDS_Shape chamfer(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges, double distance,
                     BRepTools_History* history)
{
    if (edges.empty()) {
        fail("No edges to chamfer");
    }
    if (distance <= 0.0) {
        fail("Chamfer distance must be positive");
    }
    return guarded("Chamfer failed, the distance may be too large for the selected edges", [&] {
        BRepFilletAPI_MakeChamfer op(shape);
        for (const TopoDS_Edge& edge : edges) {
            op.Add(distance, edge);
        }
        op.Build();
        if (!op.IsDone()) {
            fail("Chamfer failed, the distance may be too large for the selected edges");
        }
        record(history, shape, op);
        return op.Shape();
    });
}

TopoDS_Shape shell(const TopoDS_Shape& shape, const std::vector<TopoDS_Face>& openFaces, double thickness, bool inside,
                   BRepTools_History* history)
{
    if (thickness <= 0.0) {
        fail("Shell thickness must be positive");
    }
    std::string message = "Shell failed, the walls may be thicker than the body allows";
    if (const std::optional<double> round = inside ? smallestConvexRound(shape) : std::nullopt;
        round && thickness >= *round) {
        message =
            std::format("Shell failed, the walls must be thinner than the smallest outer round of {:g} mm", *round);
    } else if (runsIntoRound(shape, openFaces)) {
        message = "Shell failed where an open face runs smoothly into a round. Open another face or shell before the "
                  "round";
    }
    const char* failed = message.c_str();
    const double offset = inside ? -thickness : thickness;
    return guarded(failed, [&] {
        if (openFaces.empty()) {
            BRepOffsetAPI_MakeOffsetShape op;
            op.PerformByJoin(shape, offset, kOffsetTolerance, BRepOffset_Skin, false, false, GeomAbs_Intersection);
            if (!op.IsDone()) {
                fail(failed);
            }
            TopoDS_Shape offsetSolid = op.Shape();
            if (!TopExp_Explorer(offsetSolid, TopAbs_SOLID).More()) {
                BRepBuilderAPI_MakeSolid solid;
                for (TopExp_Explorer it(offsetSolid, TopAbs_SHELL); it.More(); it.Next()) {
                    solid.Add(TopoDS::Shell(it.Current()));
                }
                offsetSolid = solid.Shape();
            }
            BRepAlgoAPI_Cut cut(inside ? shape : offsetSolid, inside ? offsetSolid : shape);
            if (!cut.IsDone() || volume(cut.Shape()) < 1e-6) {
                fail(failed);
            }
            record(history, cut.History());
            return cut.Shape();
        }
        NCollection_List<TopoDS_Shape> faces;
        for (const TopoDS_Face& face : openFaces) {
            faces.Append(face);
        }
        const double before = volume(shape);
        double openArea = 0.0;
        for (const TopoDS_Face& face : openFaces) {
            openArea += area(face);
        }
        const double wallLimit = (area(shape) - openArea) * thickness * kShellWallSlack;
        const auto thicken = [&](bool intersection) -> std::optional<TopoDS_Shape> {
            try {
                BRepOffsetAPI_MakeThickSolid op;
                op.MakeThickSolidByJoin(shape, faces, offset, kOffsetTolerance, BRepOffset_Skin, intersection, false,
                                        GeomAbs_Intersection);
                if (!op.IsDone()) {
                    return std::nullopt;
                }
                Handle(BRepTools_History) steps = new BRepTools_History;
                steps->Merge(only(shape), op);
                TopoDS_Shape result = op.Shape();
                if (!BRepCheck_Analyzer(result).IsValid()) {
                    ShapeFix_Shape fix(result);
                    fix.Perform();
                    steps->Merge(fix.Context()->History());
                    result = fix.Shape();
                }
                const double after = volume(result);
                const bool plausible = inside ? after > 1e-6 && after < before && after <= wallLimit : after > 1e-6;
                if (!plausible || !BRepCheck_Analyzer(result).IsValid() ||
                    !hasEveryWall(shape, result, openFaces, thickness, inside)) {
                    return std::nullopt;
                }
                record(history, steps);
                return result;
            } catch (const Standard_Failure& failure) {
                K137_DEBUG(Geometry, "Shell attempt failed: {}", failure.what() != nullptr ? failure.what() : "");
                return std::nullopt;
            }
        };
        std::optional<TopoDS_Shape> result = thicken(false);
        if (!result) {
            result = thicken(true);
        }
        if (!result) {
            fail(failed);
        }
        return *result;
    });
}

TopoDS_Shape offsetFaces(const TopoDS_Shape& shape, const std::vector<TopoDS_Face>& faces, double distance,
                         BRepTools_History* history)
{
    if (faces.empty()) {
        fail("No faces to move");
    }
    if (std::abs(distance) < kMinDistance) {
        fail("Press Pull distance is zero");
    }
    std::vector<TopoDS_Shape> prisms;
    BRepTools_History moved;
    for (const TopoDS_Face& face : faces) {
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane) {
            fail("Press Pull moves flat faces only");
        }
        gp_Dir normal = surface.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED) {
            normal.Reverse();
        }
        prisms.push_back(guarded("Press Pull failed", [&] {
            BRepPrimAPI_MakePrism prism(face, gp_Vec(normal) * distance, true);
            if (!prism.IsDone()) {
                fail("Press Pull failed");
            }
            moved.AddModified(face, prism.LastShape(face));
            for (TopExp_Explorer edge(face, TopAbs_EDGE); edge.More(); edge.Next()) {
                for (const TopoDS_Shape& side : prism.Generated(edge.Current())) {
                    moved.AddGenerated(edge.Current(), side);
                }
            }
            return prism.Shape();
        }));
    }
    if (history != nullptr) {
        history->Merge(moved);
    }
    const TopoDS_Shape tool = fuseAll(prisms);
    if (distance > 0.0) {
        return fuse(shape, tool, history);
    }
    TopoDS_Shape result = guarded("Press Pull failed", [&] {
        BRepAlgoAPI_Cut op(shape, tool);
        if (!op.IsDone()) {
            fail("Press Pull failed");
        }
        record(history, op.History());
        return unify(op.Shape(), history);
    });
    if (isEmpty(result)) {
        fail("Press Pull removes the whole body");
    }
    return result;
}

TopoDS_Shape holeTool(const gp_Ax1& axis, const HoleParams& params)
{
    const double radius = params.diameter * 0.5;
    if (radius < kMinDistance || params.depth < kMinDistance) {
        fail("The hole diameter and depth must be positive");
    }
    const bool hasHead = params.head != HoleHead::None;
    const double headRadius = params.headDiameter * 0.5;
    if (hasHead && headRadius <= radius) {
        fail("The head diameter must be larger than the hole diameter");
    }
    std::vector<std::pair<double, double>> profile{{0.0, -kHoleEntryMargin}};
    switch (params.head) {
    case HoleHead::None:
        profile.emplace_back(radius, -kHoleEntryMargin);
        break;
    case HoleHead::Counterbore:
        if (params.headDepth < kMinDistance || params.headDepth >= params.depth) {
            fail("The counterbore must be shallower than the hole");
        }
        profile.emplace_back(headRadius, -kHoleEntryMargin);
        profile.emplace_back(headRadius, params.headDepth);
        profile.emplace_back(radius, params.headDepth);
        break;
    case HoleHead::Countersink: {
        const double halfAngle = params.countersinkAngleDegrees * 0.5 * std::numbers::pi / 180.0;
        if (halfAngle <= 0.0 || halfAngle >= std::numbers::pi * 0.5) {
            fail("The countersink angle must be between 0 and 180 degrees");
        }
        const double coneDepth = (headRadius - radius) / std::tan(halfAngle);
        if (coneDepth >= params.depth) {
            fail("The countersink must be shallower than the hole");
        }
        profile.emplace_back(headRadius, -kHoleEntryMargin);
        profile.emplace_back(headRadius, 0.0);
        profile.emplace_back(radius, coneDepth);
        break;
    }
    }
    profile.emplace_back(radius, params.depth);
    const double tipHalfAngle = params.tipAngleDegrees * 0.5 * std::numbers::pi / 180.0;
    const bool pointed = tipHalfAngle > 0.0 && tipHalfAngle < std::numbers::pi * 0.5;
    profile.emplace_back(0.0, params.depth + (pointed ? radius / std::tan(tipHalfAngle) : 0.0));

    return guarded("Hole failed", [&] {
        const gp_Ax3 frame(axis.Location(), axis.Direction());
        const gp_Vec across(frame.XDirection());
        const gp_Vec along(axis.Direction());
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto& [r, z] : profile) {
            polygon.Add(axis.Location().Translated(across * r + along * z));
        }
        polygon.Close();
        BRepBuilderAPI_MakeFace face(polygon.Wire(), true);
        if (!face.IsDone()) {
            fail("Hole failed");
        }
        BRepPrimAPI_MakeRevol revol(face.Face(), axis, 2.0 * std::numbers::pi, true);
        if (!revol.IsDone()) {
            fail("Hole failed");
        }
        return revol.Shape();
    });
}

gp_Pnt edgeMidpoint(const TopoDS_Edge& edge)
{
    BRepAdaptor_Curve curve(edge);
    return curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2.0);
}

std::optional<TopoDS_Edge> findEdge(const TopoDS_Shape& shape, const gp_Pnt& pointOnEdge, double tolerance)
{
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(pointOnEdge);
    std::optional<TopoDS_Edge> best;
    double bestDistance = std::numeric_limits<double>::max();
    double bestMidpointDistance = std::numeric_limits<double>::max();
    for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(it.Current());
        BRepExtrema_DistShapeShape distance(probe, edge);
        if (!distance.IsDone() || distance.Value() > tolerance) {
            continue;
        }
        const double midpointDistance = edgeMidpoint(edge).Distance(pointOnEdge);
        if (distance.Value() < bestDistance - tolerance ||
            (distance.Value() <= bestDistance + tolerance && midpointDistance < bestMidpointDistance)) {
            bestDistance = distance.Value();
            bestMidpointDistance = midpointDistance;
            best = edge;
        }
    }
    return best;
}

std::optional<gp_Ax3> planarFrame(const TopoDS_Face& face)
{
    constexpr double kParallel = 1e-6;
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) {
        return std::nullopt;
    }
    const gp_Pln plane = surface.Plane();
    gp_Dir normal = plane.Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }
    const gp_Vec n(normal);
    gp_Vec x(1.0, 0.0, 0.0);
    x -= n * x.Dot(n);
    if (x.Magnitude() < kParallel) {
        x = gp_Vec(0.0, 1.0, 0.0);
        x -= n * x.Dot(n);
    }
    const gp_Vec toOrigin(plane.Location(), gp::Origin());
    const gp_Pnt origin = gp::Origin().Translated(-n * toOrigin.Dot(n));
    return gp_Ax3(origin, normal, gp_Dir(x));
}

gp_Pnt facePoint(const TopoDS_Face& face)
{
    const BRepAdaptor_Surface surface(face);
    double uMin = 0.0;
    double uMax = 0.0;
    double vMin = 0.0;
    double vMax = 0.0;
    BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
    constexpr int kSteps = 8;
    std::optional<gp_Pnt2d> best;
    double bestOffset = std::numeric_limits<double>::max();
    for (int i = 1; i < kSteps; ++i) {
        for (int j = 1; j < kSteps; ++j) {
            const gp_Pnt2d uv(uMin + (uMax - uMin) * i / kSteps, vMin + (vMax - vMin) * j / kSteps);
            const double offset = std::abs(i - kSteps / 2) + std::abs(j - kSteps / 2);
            if (offset >= bestOffset) {
                continue;
            }
            BRepClass_FaceClassifier classifier(face, uv, Precision::Confusion());
            if (classifier.State() == TopAbs_IN) {
                best = uv;
                bestOffset = offset;
            }
        }
    }
    const gp_Pnt2d uv = best.value_or(gp_Pnt2d((uMin + uMax) / 2.0, (vMin + vMax) / 2.0));
    return surface.Value(uv.X(), uv.Y());
}

std::optional<TopoDS_Face> findFace(const TopoDS_Shape& shape, const gp_Pnt& pointOnFace, double tolerance)
{
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(pointOnFace);
    std::optional<TopoDS_Face> best;
    double bestDistance = tolerance;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face& face = TopoDS::Face(it.Current());
        BRepExtrema_DistShapeShape distance(probe, face);
        if (distance.IsDone() && distance.Value() <= bestDistance) {
            bestDistance = distance.Value();
            best = face;
        }
    }
    return best;
}

} // namespace kalve137::geometry
