// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/feature.h"

#include <gp_Dir.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <initializer_list>
#include <numbers>
#include <stdexcept>

namespace kalve137::core {

namespace {

constexpr double kParallelCosine = 1.0 - 1e-9;
constexpr double kMinLength = 1e-9;

template <typename Enum>
std::optional<Enum> enumFromString(std::string_view name, std::initializer_list<Enum> values)
{
    for (Enum value : values) {
        if (toString(value) == name) {
            return value;
        }
    }
    return std::nullopt;
}

gp_Pnt projected(const gp_Pnt& point, const gp_Ax3& plane)
{
    const gp_Vec normal(plane.Direction());
    return point.Translated(-normal * gp_Vec(plane.Location(), point).Dot(normal));
}

ConstructionPlane anglePlane(const PlaneFeature& plane)
{
    const gp_Dir along = plane.hinge.Direction();
    gp_Vec normal(plane.base.Direction());
    normal -= gp_Vec(along) * normal.Dot(gp_Vec(along));
    if (normal.Magnitude() < kMinLength) {
        throw std::runtime_error("The line runs along the plane normal, so it cannot turn the plane");
    }
    const gp_Dir turned =
        gp_Dir(normal).Rotated(gp_Ax1(gp::Origin(), along), plane.angleDegrees * std::numbers::pi / 180.0);
    return {gp_Ax3(plane.center, turned, along), plane.center};
}

ConstructionPlane midplane(const PlaneFeature& plane)
{
    if (plane.secondName.empty()) {
        throw std::runtime_error("Pick a second plane or face");
    }
    const gp_Dir a = plane.base.Direction();
    const gp_Dir b = plane.second.Direction();
    const gp_Pnt middle((plane.center.XYZ() + plane.secondCenter.XYZ()) * 0.5);
    const double cosine = a.Dot(b);
    if (std::abs(cosine) >= kParallelCosine) {
        const double gap = gp_Vec(plane.base.Location(), plane.second.Location()).Dot(gp_Vec(a));
        const gp_Ax3 frame = plane.base.Translated(gp_Vec(a) * (gap * 0.5));
        return {frame, projected(middle, frame)};
    }
    const gp_Dir normal(gp_Vec(a) - gp_Vec(b));
    const gp_Dir line = a.Crossed(b);
    const double da = gp_Vec(plane.base.Location().XYZ()).Dot(gp_Vec(a));
    const double db = gp_Vec(plane.second.Location().XYZ()).Dot(gp_Vec(b));
    const double det = 1.0 - cosine * cosine;
    const gp_Pnt onLine((gp_Vec(a) * ((da - db * cosine) / det) + gp_Vec(b) * ((db - da * cosine) / det)).XYZ());
    const gp_Ax3 frame(onLine, normal, line);
    return {frame, projected(middle, frame)};
}

} // namespace

gp_Ax3 originPlane(OriginPlane plane)
{
    const gp_Pnt origin(0.0, 0.0, 0.0);
    switch (plane) {
    case OriginPlane::XY:
        return gp_Ax3(origin, gp_Dir(0.0, 0.0, 1.0), gp_Dir(1.0, 0.0, 0.0));
    case OriginPlane::XZ:
        return gp_Ax3(origin, gp_Dir(0.0, -1.0, 0.0), gp_Dir(1.0, 0.0, 0.0));
    case OriginPlane::YZ:
        return gp_Ax3(origin, gp_Dir(1.0, 0.0, 0.0), gp_Dir(0.0, 1.0, 0.0));
    }
    return gp_Ax3();
}

ConstructionPlane constructionPlane(const PlaneFeature& plane)
{
    switch (plane.kind) {
    case PlaneKind::Offset: {
        const gp_Vec shift = gp_Vec(plane.base.Direction()) * plane.offset;
        return {plane.base.Translated(shift), plane.center.Translated(shift)};
    }
    case PlaneKind::Angle:
        return anglePlane(plane);
    case PlaneKind::Midplane:
        return midplane(plane);
    }
    throw std::runtime_error("Unknown plane kind");
}

std::optional<gp_Ax1> axisThrough(const gp_Pnt& a, const gp_Pnt& b)
{
    if (a.Distance(b) < kMinLength) {
        return std::nullopt;
    }
    return gp_Ax1(a, gp_Dir(gp_Vec(a, b)));
}

std::string_view toString(OriginPlane plane)
{
    switch (plane) {
    case OriginPlane::XY:
        return "XY";
    case OriginPlane::XZ:
        return "XZ";
    case OriginPlane::YZ:
        return "YZ";
    }
    return {};
}

std::string_view toString(PlaneKind kind)
{
    switch (kind) {
    case PlaneKind::Offset:
        return "offset";
    case PlaneKind::Angle:
        return "angle";
    case PlaneKind::Midplane:
        return "midplane";
    }
    return {};
}

std::optional<PlaneKind> planeKindFromString(std::string_view name)
{
    return enumFromString(name, {PlaneKind::Offset, PlaneKind::Angle, PlaneKind::Midplane});
}

std::string_view toString(AxisSource source)
{
    switch (source) {
    case AxisSource::Edge:
        return "edge";
    case AxisSource::Cylinder:
        return "cylinder";
    case AxisSource::TwoPoints:
        return "two_points";
    case AxisSource::FaceNormal:
        return "face_normal";
    }
    return {};
}

std::optional<AxisSource> axisSourceFromString(std::string_view name)
{
    return enumFromString(name,
                          {AxisSource::Edge, AxisSource::Cylinder, AxisSource::TwoPoints, AxisSource::FaceNormal});
}

std::string_view toString(Operation op)
{
    switch (op) {
    case Operation::NewBody:
        return "new_body";
    case Operation::Join:
        return "join";
    case Operation::Cut:
        return "cut";
    case Operation::Intersect:
        return "intersect";
    }
    return {};
}

std::optional<Operation> operationFromString(std::string_view name)
{
    return enumFromString(name, {Operation::NewBody, Operation::Join, Operation::Cut, Operation::Intersect});
}

std::string_view toString(ExtrudeExtent extent)
{
    switch (extent) {
    case ExtrudeExtent::OneSide:
        return "one_side";
    case ExtrudeExtent::Symmetric:
        return "symmetric";
    case ExtrudeExtent::TwoSides:
        return "two_sides";
    }
    return {};
}

std::optional<ExtrudeExtent> extrudeExtentFromString(std::string_view name)
{
    return enumFromString(name, {ExtrudeExtent::OneSide, ExtrudeExtent::Symmetric, ExtrudeExtent::TwoSides});
}

std::string_view toString(ExtrudeEnd end)
{
    switch (end) {
    case ExtrudeEnd::Distance:
        return "distance";
    case ExtrudeEnd::ToObject:
        return "to_object";
    case ExtrudeEnd::All:
        return "all";
    }
    return {};
}

std::optional<ExtrudeEnd> extrudeEndFromString(std::string_view name)
{
    return enumFromString(name, {ExtrudeEnd::Distance, ExtrudeEnd::ToObject, ExtrudeEnd::All});
}

std::string_view toString(AxisKind kind)
{
    switch (kind) {
    case AxisKind::SketchLine:
        return "sketch_line";
    case AxisKind::X:
        return "x";
    case AxisKind::Y:
        return "y";
    case AxisKind::Z:
        return "z";
    case AxisKind::Construction:
        return "construction";
    }
    return {};
}

std::optional<AxisKind> axisKindFromString(std::string_view name)
{
    return enumFromString(name, {AxisKind::SketchLine, AxisKind::X, AxisKind::Y, AxisKind::Z, AxisKind::Construction});
}

std::string_view toString(HoleKind kind)
{
    switch (kind) {
    case HoleKind::Simple:
        return "simple";
    case HoleKind::Counterbore:
        return "counterbore";
    case HoleKind::Countersink:
        return "countersink";
    }
    return {};
}

std::optional<HoleKind> holeKindFromString(std::string_view name)
{
    return enumFromString(name, {HoleKind::Simple, HoleKind::Counterbore, HoleKind::Countersink});
}

std::string_view typeName(const FeatureData& data)
{
    return std::visit(Overloaded{
                          [](const SketchFeature&) { return std::string_view("sketch"); },
                          [](const ExtrudeFeature&) { return std::string_view("extrude"); },
                          [](const RevolveFeature&) { return std::string_view("revolve"); },
                          [](const FilletFeature&) { return std::string_view("fillet"); },
                          [](const ChamferFeature&) { return std::string_view("chamfer"); },
                          [](const ShellFeature&) { return std::string_view("shell"); },
                          [](const PatternFeature&) { return std::string_view("pattern"); },
                          [](const ImportFeature&) { return std::string_view("import"); },
                          [](const PlaneFeature&) { return std::string_view("plane"); },
                          [](const OffsetFaceFeature&) { return std::string_view("offset_face"); },
                          [](const HoleFeature&) { return std::string_view("hole"); },
                          [](const MoveFeature&) { return std::string_view("move"); },
                          [](const AxisFeature&) { return std::string_view("axis"); },
                          [](const PointFeature&) { return std::string_view("point"); },
                          [](const AppearanceFeature&) { return std::string_view("appearance"); },
                      },
                      data);
}

} // namespace kalve137::core
