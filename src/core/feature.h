// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "geometry/exchange.h"
#include "sketch/sketch.h"

#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace kalve137::core {

using FeatureId = int;

struct BodyKey {
    FeatureId feature = 0;
    int part = 0;

    auto operator<=>(const BodyKey&) const = default;
};

enum class OriginPlane { XY, XZ, YZ };
gp_Ax3 originPlane(OriginPlane plane);
std::string_view toString(OriginPlane plane);

struct TopoRef {
    TopoRef() = default;
    TopoRef(const gp_Pnt& at) : point(at) {} // NOLINT(google-explicit-constructor) point-only refs match by geometry
    TopoRef(const gp_Pnt& at, std::vector<std::string> faceNames) : point(at), names(std::move(faceNames)) {}

    gp_Pnt point;
    std::vector<std::string> names;
};

struct SketchFeature {
    gp_Ax3 plane = originPlane(OriginPlane::XY);
    std::string planeName = "XY";
    FeatureId planeFeature = 0;
    std::optional<TopoRef> face;
    sketch::Sketch sketch;
};

enum class PlaneKind { Offset, Angle, Midplane };
std::string_view toString(PlaneKind kind);
std::optional<PlaneKind> planeKindFromString(std::string_view name);

struct PlaneFeature {
    PlaneKind kind = PlaneKind::Offset;
    gp_Ax3 base = originPlane(OriginPlane::XY);
    std::string baseName;
    gp_Pnt center;
    double offset = 10.0;
    gp_Ax1 hinge;
    double angleDegrees = 45.0;
    gp_Ax3 second = originPlane(OriginPlane::XY);
    std::string secondName;
    gp_Pnt secondCenter;
};

struct ConstructionPlane {
    gp_Ax3 frame;
    gp_Pnt center;
};
ConstructionPlane constructionPlane(const PlaneFeature& plane);

enum class AxisSource { Edge, Cylinder, TwoPoints, FaceNormal };
std::string_view toString(AxisSource source);
std::optional<AxisSource> axisSourceFromString(std::string_view name);

std::optional<gp_Ax1> axisThrough(const gp_Pnt& a, const gp_Pnt& b);

struct AxisFeature {
    AxisSource kind = AxisSource::Edge;
    gp_Ax1 axis;
    std::string baseName;
};

struct PointFeature {
    gp_Pnt point;
    std::string baseName;
};

enum class Operation { NewBody, Join, Cut, Intersect };
std::string_view toString(Operation op);
std::optional<Operation> operationFromString(std::string_view name);

struct ProfileRef {
    FeatureId sketch = 0;
    std::vector<sketch::Vec2> seeds;
};

enum class ExtrudeExtent { OneSide, Symmetric, TwoSides };
std::string_view toString(ExtrudeExtent extent);
std::optional<ExtrudeExtent> extrudeExtentFromString(std::string_view name);

enum class ExtrudeEnd { Distance, ToObject, All };
std::string_view toString(ExtrudeEnd end);
std::optional<ExtrudeEnd> extrudeEndFromString(std::string_view name);

struct ExtrudeFeature {
    ProfileRef profiles;
    double distance = 10.0;
    ExtrudeExtent extent = ExtrudeExtent::OneSide;
    double secondDistance = 10.0;
    Operation operation = Operation::NewBody;
    std::string bodyName;
    ExtrudeEnd end = ExtrudeEnd::Distance;
    std::optional<TopoRef> toFace;
    FeatureId toPlane = 0;
    double taperDegrees = 0.0;
};

enum class AxisKind { SketchLine, X, Y, Z, Construction };
std::string_view toString(AxisKind kind);
std::optional<AxisKind> axisKindFromString(std::string_view name);

struct AxisRef {
    AxisKind kind = AxisKind::Z;
    FeatureId sketch = 0;
    sketch::Id line = 0;
    FeatureId construction = 0;
};

struct RevolveFeature {
    ProfileRef profiles;
    AxisRef axis;
    double angleDegrees = 360.0;
    Operation operation = Operation::NewBody;
    std::string bodyName;
};

struct FilletSet {
    std::vector<TopoRef> edges;
    double radius = 1.0;
};

struct FilletFeature {
    std::vector<FilletSet> sets{FilletSet{}};
};

struct ChamferFeature {
    std::vector<TopoRef> edges;
    double distance = 1.0;
};

struct ShellFeature {
    std::vector<TopoRef> faces;
    double thickness = 2.0;
    bool inside = true;
};

enum class PatternKind { Linear, Circular };

inline constexpr int kMinPatternCount = 2;
inline constexpr int kMaxPatternCount = 100;

struct PatternFeature {
    BodyKey body;
    PatternKind kind = PatternKind::Linear;
    AxisRef axis{AxisKind::X, 0, 0};
    int count = 3;
    double spacing = 20.0;
    double angleDegrees = 360.0;
};

struct ImportFeature {
    std::string source;
    std::vector<geometry::StepPart> parts;
};

struct OffsetFaceFeature {
    std::vector<TopoRef> faces;
    double distance = 5.0;
};

enum class HoleKind { Simple, Counterbore, Countersink };
std::string_view toString(HoleKind kind);
std::optional<HoleKind> holeKindFromString(std::string_view name);

struct HoleFeature {
    HoleKind kind = HoleKind::Simple;
    std::vector<TopoRef> facePoints;
    FeatureId sketch = 0;
    std::vector<sketch::Id> sketchPoints;
    double diameter = 5.0;
    double depth = 10.0;
    bool throughAll = false;
    double headDiameter = 9.0;
    double headDepth = 3.0;
    double countersinkAngle = 90.0;
    double tipAngle = 118.0;
    bool flip = false;
};

struct MoveFeature {
    std::vector<BodyKey> bodies;
    gp_Vec translation{0.0, 0.0, 0.0};
    AxisKind rotateAxis = AxisKind::Z;
    double angleDegrees = 0.0;
    gp_Pnt pivot;
    bool copy = false;
    std::vector<std::string> copyNames;
};

struct AppearanceFeature {
    std::vector<BodyKey> bodies;
    std::vector<TopoRef> faces;
    std::optional<geometry::Color> color;
    bool clear = false;
};

using FeatureData = std::variant<SketchFeature, ExtrudeFeature, RevolveFeature, FilletFeature, ChamferFeature,
                                 ShellFeature, PatternFeature, ImportFeature, PlaneFeature, OffsetFaceFeature,
                                 HoleFeature, MoveFeature, AxisFeature, PointFeature, AppearanceFeature>;

std::string_view typeName(const FeatureData& data);

struct Feature {
    FeatureId id = 0;
    std::string name;
    bool suppressed = false;
    FeatureData data;
};

template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

} // namespace kalve137::core
