// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/serialization.h"

#include "geometry/exchange.h"
#include "log/log.h"

#include <nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace kalve137::core {

namespace {

using nlohmann::json;

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

json toJson(const gp_Pnt& p)
{
    return json::array({p.X(), p.Y(), p.Z()});
}

json toJson(const gp_Dir& d)
{
    return json::array({d.X(), d.Y(), d.Z()});
}

gp_Pnt pointFrom(const json& j)
{
    return gp_Pnt(j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>());
}

gp_Dir dirFrom(const json& j)
{
    return gp_Dir(j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>());
}

json planeToJson(const gp_Ax3& plane)
{
    return {{"origin", toJson(plane.Location())},
            {"normal", toJson(plane.Direction())},
            {"x_dir", toJson(plane.XDirection())}};
}

gp_Ax3 planeFrom(const json& j)
{
    return gp_Ax3(pointFrom(j.at("origin")), dirFrom(j.at("normal")), dirFrom(j.at("x_dir")));
}

json sketchToJson(const sketch::Sketch& s)
{
    json points = json::array();
    for (const auto& [id, p] : s.points()) {
        points.push_back({{"id", id}, {"x", p.pos.x}, {"y", p.pos.y}, {"fixed", p.fixed}});
    }
    json curves = json::array();
    for (const auto& [id, c] : s.curves()) {
        curves.push_back({{"id", id},
                          {"kind", sketch::toString(c.kind)},
                          {"points", c.points},
                          {"radius", c.radius},
                          {"construction", c.construction},
                          {"fixed", c.fixed}});
    }
    json constraints = json::array();
    for (const auto& [id, c] : s.constraints()) {
        json constraint = {{"id", id}, {"kind", sketch::toString(c.kind)}, {"refs", c.refs}, {"value", c.value}};
        if (c.labelOffset) {
            constraint["label"] = {c.labelOffset->x, c.labelOffset->y};
        }
        constraints.push_back(std::move(constraint));
    }
    return {{"points", points}, {"curves", curves}, {"constraints", constraints}};
}

sketch::Sketch sketchFrom(const json& j)
{
    sketch::Sketch s;
    for (const json& p : j.at("points")) {
        s.insertPoint(p.at("id").get<int>(),
                      {{p.at("x").get<double>(), p.at("y").get<double>()}, p.value("fixed", false)});
    }
    for (const json& c : j.at("curves")) {
        const auto kind = sketch::curveKindFromString(c.at("kind").get<std::string>());
        if (!kind) {
            throw std::runtime_error("Unknown sketch curve kind " + c.at("kind").get<std::string>());
        }
        sketch::Curve curve;
        curve.kind = *kind;
        curve.points = c.at("points").get<std::vector<int>>();
        curve.radius = c.value("radius", 0.0);
        curve.construction = c.value("construction", false);
        curve.fixed = c.value("fixed", false);
        s.insertCurve(c.at("id").get<int>(), std::move(curve));
    }
    for (const json& c : j.at("constraints")) {
        const auto kind = sketch::constraintKindFromString(c.at("kind").get<std::string>());
        if (!kind) {
            throw std::runtime_error("Unknown sketch constraint kind " + c.at("kind").get<std::string>());
        }
        sketch::Constraint constraint{*kind, c.at("refs").get<std::vector<int>>(), c.value("value", 0.0)};
        if (const auto label = c.find("label"); label != c.end()) {
            const auto xy = label->get<std::array<double, 2>>();
            constraint.labelOffset = sketch::Vec2{xy[0], xy[1]};
        }
        s.insertConstraint(c.at("id").get<int>(), std::move(constraint));
    }
    if (const std::optional<std::string> problem = sketch::validate(s)) {
        throw std::runtime_error("A sketch in the design is damaged: " + *problem);
    }
    return s;
}

json profilesToJson(const ProfileRef& ref)
{
    json seeds = json::array();
    for (const sketch::Vec2& seed : ref.seeds) {
        seeds.push_back(json::array({seed.x, seed.y}));
    }
    return {{"sketch", ref.sketch}, {"seeds", seeds}};
}

ProfileRef profilesFrom(const json& j)
{
    ProfileRef ref;
    ref.sketch = j.at("sketch").get<int>();
    for (const json& seed : j.at("seeds")) {
        ref.seeds.push_back({seed.at(0).get<double>(), seed.at(1).get<double>()});
    }
    return ref;
}

template <typename Parse>
auto enumFrom(const json& j, Parse parse, const char* what)
{
    const std::string name = j.get<std::string>();
    const auto value = parse(name);
    if (!value) {
        throw std::runtime_error(std::string("Unknown ") + what + " " + name);
    }
    return *value;
}

Operation operationFrom(const json& j)
{
    return enumFrom(j, operationFromString, "operation");
}

json refsToJson(const std::vector<TopoRef>& refs)
{
    json result = json::array();
    for (const TopoRef& ref : refs) {
        result.push_back({{"point", toJson(ref.point)}, {"names", ref.names}});
    }
    return result;
}

std::vector<TopoRef> refsFrom(const json& j)
{
    std::vector<TopoRef> result;
    for (const json& ref : j) {
        if (ref.is_array()) {
            result.push_back({pointFrom(ref), {}});
            continue;
        }
        result.push_back({pointFrom(ref.at("point")), ref.value("names", std::vector<std::string>())});
    }
    return result;
}

json axisToJson(const AxisRef& axis)
{
    json result = {{"kind", toString(axis.kind)}, {"sketch", axis.sketch}, {"line", axis.line}};
    if (axis.construction != 0) {
        result["construction"] = axis.construction;
    }
    return result;
}

AxisRef axisFrom(const json& j)
{
    return {enumFrom(j.at("kind"), axisKindFromString, "axis"), j.value("sketch", 0), j.value("line", 0),
            j.value("construction", 0)};
}

json lineToJson(const gp_Ax1& line)
{
    return {{"origin", toJson(line.Location())}, {"direction", toJson(line.Direction())}};
}

gp_Ax1 lineFrom(const json& j)
{
    return gp_Ax1(pointFrom(j.at("origin")), dirFrom(j.at("direction")));
}

json vecToJson(const gp_Vec& v)
{
    return json::array({v.X(), v.Y(), v.Z()});
}

gp_Vec vecFrom(const json& j)
{
    return gp_Vec(j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>());
}

json planeFeatureToJson(const PlaneFeature& f)
{
    json result = {{"kind", toString(f.kind)},
                   {"base", planeToJson(f.base)},
                   {"base_name", f.baseName},
                   {"center", toJson(f.center)}};
    switch (f.kind) {
    case PlaneKind::Offset:
        result["offset"] = f.offset;
        break;
    case PlaneKind::Angle:
        result["hinge"] = lineToJson(f.hinge);
        result["angle"] = f.angleDegrees;
        break;
    case PlaneKind::Midplane:
        result["second"] = planeToJson(f.second);
        result["second_name"] = f.secondName;
        result["second_center"] = toJson(f.secondCenter);
        break;
    }
    return result;
}

PlaneFeature planeFeatureFrom(const json& j)
{
    PlaneFeature f;
    f.kind = j.contains("kind") ? enumFrom(j.at("kind"), planeKindFromString, "plane kind") : PlaneKind::Offset;
    f.base = planeFrom(j.at("base"));
    f.baseName = j.at("base_name").get<std::string>();
    f.center = pointFrom(j.at("center"));
    f.offset = j.value("offset", f.offset);
    if (j.contains("hinge")) {
        f.hinge = lineFrom(j.at("hinge"));
    }
    f.angleDegrees = j.value("angle", f.angleDegrees);
    if (j.contains("second")) {
        f.second = planeFrom(j.at("second"));
        f.secondName = j.value("second_name", std::string());
        f.secondCenter = pointFrom(j.at("second_center"));
    }
    return f;
}

json holeToJson(const HoleFeature& f)
{
    return {{"kind", toString(f.kind)},    {"face_points", refsToJson(f.facePoints)},
            {"sketch", f.sketch},          {"sketch_points", f.sketchPoints},
            {"diameter", f.diameter},      {"depth", f.depth},
            {"through_all", f.throughAll}, {"head_diameter", f.headDiameter},
            {"head_depth", f.headDepth},   {"countersink_angle", f.countersinkAngle},
            {"tip_angle", f.tipAngle},     {"flip", f.flip}};
}

HoleFeature holeFrom(const json& j)
{
    HoleFeature f;
    f.kind = enumFrom(j.at("kind"), holeKindFromString, "hole kind");
    f.facePoints = refsFrom(j.at("face_points"));
    f.sketch = j.value("sketch", 0);
    f.sketchPoints = j.value("sketch_points", std::vector<sketch::Id>());
    f.diameter = j.at("diameter").get<double>();
    f.depth = j.at("depth").get<double>();
    f.throughAll = j.value("through_all", false);
    f.headDiameter = j.value("head_diameter", f.headDiameter);
    f.headDepth = j.value("head_depth", f.headDepth);
    f.countersinkAngle = j.value("countersink_angle", f.countersinkAngle);
    f.tipAngle = j.value("tip_angle", f.tipAngle);
    f.flip = j.value("flip", false);
    return f;
}

json moveToJson(const MoveFeature& f)
{
    json bodies = json::array();
    for (const BodyKey& key : f.bodies) {
        bodies.push_back(json::array({key.feature, key.part}));
    }
    return {{"bodies", std::move(bodies)},
            {"translation", vecToJson(f.translation)},
            {"rotate_axis", toString(f.rotateAxis)},
            {"angle", f.angleDegrees},
            {"pivot", toJson(f.pivot)},
            {"copy", f.copy},
            {"copy_names", f.copyNames}};
}

MoveFeature moveFrom(const json& j)
{
    MoveFeature f;
    for (const json& key : j.at("bodies")) {
        f.bodies.push_back({key.at(0).get<int>(), key.at(1).get<int>()});
    }
    f.translation = vecFrom(j.at("translation"));
    f.rotateAxis = enumFrom(j.at("rotate_axis"), axisKindFromString, "axis");
    f.angleDegrees = j.value("angle", 0.0);
    f.pivot = pointFrom(j.at("pivot"));
    f.copy = j.value("copy", false);
    f.copyNames = j.value("copy_names", std::vector<std::string>());
    return f;
}

json appearanceToJson(const AppearanceFeature& f)
{
    json bodies = json::array();
    for (const BodyKey& key : f.bodies) {
        bodies.push_back(json::array({key.feature, key.part}));
    }
    json result = {{"bodies", std::move(bodies)}, {"faces", refsToJson(f.faces)}};
    if (f.color) {
        result["color"] = json::array({f.color->red, f.color->green, f.color->blue});
    }
    if (f.clear) {
        result["clear"] = true;
    }
    return result;
}

AppearanceFeature appearanceFrom(const json& j)
{
    AppearanceFeature f;
    for (const json& key : j.at("bodies")) {
        f.bodies.push_back({key.at(0).get<int>(), key.at(1).get<int>()});
    }
    f.faces = refsFrom(j.at("faces"));
    if (j.contains("color")) {
        const json& c = j.at("color");
        f.color = geometry::Color{c.at(0).get<double>(), c.at(1).get<double>(), c.at(2).get<double>()};
    }
    f.clear = j.value("clear", false);
    return f;
}

json dataToJson(const FeatureData& data, ShapeStore& shapes)
{
    return std::visit(
        Overloaded{
            [](const SketchFeature& f) -> json {
                json result = {
                    {"plane", planeToJson(f.plane)}, {"plane_name", f.planeName}, {"sketch", sketchToJson(f.sketch)}};
                if (f.planeFeature != 0) {
                    result["plane_feature"] = f.planeFeature;
                }
                if (f.face) {
                    result["face"] = refsToJson({*f.face}).front();
                }
                return result;
            },
            [](const PlaneFeature& f) -> json { return planeFeatureToJson(f); },
            [](const OffsetFaceFeature& f) -> json {
                return {{"faces", refsToJson(f.faces)}, {"distance", f.distance}};
            },
            [](const HoleFeature& f) -> json { return holeToJson(f); },
            [](const MoveFeature& f) -> json { return moveToJson(f); },
            [](const AxisFeature& f) -> json {
                return {{"source", toString(f.kind)}, {"axis", lineToJson(f.axis)}, {"base_name", f.baseName}};
            },
            [](const PointFeature& f) -> json { return {{"point", toJson(f.point)}, {"base_name", f.baseName}}; },
            [](const AppearanceFeature& f) -> json { return appearanceToJson(f); },
            [](const ExtrudeFeature& f) -> json {
                json result = {{"profiles", profilesToJson(f.profiles)}, {"distance", f.distance},
                               {"extent", toString(f.extent)},           {"second_distance", f.secondDistance},
                               {"operation", toString(f.operation)},     {"body_name", f.bodyName}};
                if (f.end != ExtrudeEnd::Distance) {
                    result["end"] = toString(f.end);
                }
                if (f.toFace) {
                    result["to_face"] = refsToJson({*f.toFace}).front();
                }
                if (f.toPlane != 0) {
                    result["to_plane"] = f.toPlane;
                }
                if (f.taperDegrees != 0.0) {
                    result["taper"] = f.taperDegrees;
                }
                return result;
            },
            [](const RevolveFeature& f) -> json {
                return {{"profiles", profilesToJson(f.profiles)},
                        {"axis", axisToJson(f.axis)},
                        {"angle", f.angleDegrees},
                        {"operation", toString(f.operation)},
                        {"body_name", f.bodyName}};
            },
            [](const FilletFeature& f) -> json {
                json sets = json::array();
                for (const FilletSet& set : f.sets) {
                    sets.push_back({{"edges", refsToJson(set.edges)}, {"radius", set.radius}});
                }
                return {{"sets", std::move(sets)}};
            },
            [](const ChamferFeature& f) -> json { return {{"edges", refsToJson(f.edges)}, {"distance", f.distance}}; },
            [](const ShellFeature& f) -> json {
                return {{"faces", refsToJson(f.faces)}, {"thickness", f.thickness}, {"inside", f.inside}};
            },
            [](const PatternFeature& f) -> json {
                return {{"body", f.body.feature},
                        {"body_part", f.body.part},
                        {"kind", f.kind == PatternKind::Circular ? "circular" : "linear"},
                        {"axis", axisToJson(f.axis)},
                        {"count", f.count},
                        {"spacing", f.spacing},
                        {"angle", f.angleDegrees}};
            },
            [&](const ImportFeature& f) -> json {
                json parts = json::array();
                for (const geometry::StepPart& part : f.parts) {
                    json entry = {{"name", part.name}, {"shape", shapes.put(part.shape)}};
                    if (part.color) {
                        entry["color"] = json::array({part.color->red, part.color->green, part.color->blue});
                    }
                    if (const auto faces = part.faceColors.toIndices(part.shape); !faces.empty()) {
                        json colors = json::array();
                        for (const auto& [index, color] : faces) {
                            colors.push_back(json::array({index, color.red, color.green, color.blue}));
                        }
                        entry["face_colors"] = std::move(colors);
                    }
                    if (!part.path.empty()) {
                        entry["path"] = part.path;
                    }
                    parts.push_back(std::move(entry));
                }
                return {{"source", f.source}, {"parts", std::move(parts)}};
            },
        },
        data);
}

TopoDS_Shape partShapeFrom(const json& j, ShapeStore& shapes)
{
    if (j.contains("shape")) {
        return shapes.get(j.at("shape").get<std::string>());
    }
    return geometry::fromBrep(j.at("brep").get<std::string>());
}

FeatureData dataFrom(const std::string& type, const json& j, ShapeStore& shapes)
{
    if (type == "sketch") {
        SketchFeature f;
        f.plane = planeFrom(j.at("plane"));
        f.planeName = j.value("plane_name", std::string("XY"));
        f.planeFeature = j.value("plane_feature", 0);
        if (j.contains("face")) {
            f.face = refsFrom(json::array({j.at("face")})).front();
        }
        f.sketch = sketchFrom(j.at("sketch"));
        return f;
    }
    if (type == "plane") {
        return planeFeatureFrom(j);
    }
    if (type == "offset_face") {
        return OffsetFaceFeature{refsFrom(j.at("faces")), j.at("distance").get<double>()};
    }
    if (type == "hole") {
        return holeFrom(j);
    }
    if (type == "move") {
        return moveFrom(j);
    }
    if (type == "axis") {
        return AxisFeature{enumFrom(j.at("source"), axisSourceFromString, "axis source"), lineFrom(j.at("axis")),
                           j.at("base_name").get<std::string>()};
    }
    if (type == "point") {
        return PointFeature{pointFrom(j.at("point")), j.at("base_name").get<std::string>()};
    }
    if (type == "appearance") {
        return appearanceFrom(j);
    }
    if (type == "extrude") {
        ExtrudeFeature f;
        f.profiles = profilesFrom(j.at("profiles"));
        f.distance = j.at("distance").get<double>();
        if (j.contains("extent")) {
            const auto extent = extrudeExtentFromString(j.at("extent").get<std::string>());
            if (!extent) {
                throw std::runtime_error("Unknown extent " + j.at("extent").get<std::string>());
            }
            f.extent = *extent;
        } else if (j.value("symmetric", false)) {
            f.extent = ExtrudeExtent::Symmetric;
        }
        f.secondDistance = j.value("second_distance", f.secondDistance);
        f.operation = operationFrom(j.at("operation"));
        f.bodyName = j.value("body_name", std::string());
        if (j.contains("end")) {
            f.end = enumFrom(j.at("end"), extrudeEndFromString, "extrude end");
        }
        if (j.contains("to_face")) {
            f.toFace = refsFrom(json::array({j.at("to_face")})).front();
        }
        f.toPlane = j.value("to_plane", 0);
        f.taperDegrees = j.value("taper", 0.0);
        return f;
    }
    if (type == "revolve") {
        RevolveFeature f;
        f.profiles = profilesFrom(j.at("profiles"));
        f.axis = axisFrom(j.at("axis"));
        f.angleDegrees = j.at("angle").get<double>();
        f.operation = operationFrom(j.at("operation"));
        f.bodyName = j.value("body_name", std::string());
        return f;
    }
    if (type == "fillet") {
        if (!j.contains("sets")) {
            return FilletFeature{{FilletSet{refsFrom(j.at("edges")), j.at("radius").get<double>()}}};
        }
        FilletFeature f;
        f.sets.clear();
        for (const json& set : j.at("sets")) {
            f.sets.push_back({refsFrom(set.at("edges")), set.at("radius").get<double>()});
        }
        if (f.sets.empty()) {
            throw std::runtime_error("The design file is damaged: a fillet has no sets");
        }
        return f;
    }
    if (type == "chamfer") {
        return ChamferFeature{refsFrom(j.at("edges")), j.at("distance").get<double>()};
    }
    if (type == "shell") {
        return ShellFeature{refsFrom(j.at("faces")), j.at("thickness").get<double>(), j.value("inside", true)};
    }
    if (type == "pattern") {
        PatternFeature f;
        f.body = {j.at("body").get<int>(), j.value("body_part", 0)};
        f.kind = j.value("kind", std::string("linear")) == "circular" ? PatternKind::Circular : PatternKind::Linear;
        f.axis = axisFrom(j.at("axis"));
        f.count = j.at("count").get<int>();
        if (f.count > kMaxPatternCount) {
            throw std::runtime_error("A pattern in the design has " + std::to_string(f.count) + " copies, more than " +
                                     std::to_string(kMaxPatternCount));
        }
        f.spacing = j.value("spacing", 20.0);
        f.angleDegrees = j.value("angle", 360.0);
        return f;
    }
    if (type == "import") {
        ImportFeature f;
        f.source = j.value("source", std::string());
        if (!j.contains("parts")) {
            f.parts.push_back(
                {j.value("body_name", std::string()), geometry::fromBrep(j.at("brep").get<std::string>()), {}});
            return f;
        }
        for (const json& part : j.at("parts")) {
            geometry::StepPart p{part.value("name", std::string()), partShapeFrom(part, shapes), {}};
            if (part.contains("color")) {
                const json& c = part.at("color");
                p.color = geometry::Color{c.at(0).get<double>(), c.at(1).get<double>(), c.at(2).get<double>()};
            }
            if (part.contains("face_colors")) {
                std::vector<std::pair<int, geometry::Color>> faces;
                for (const json& c : part.at("face_colors")) {
                    faces.emplace_back(c.at(0).get<int>(), geometry::Color{c.at(1).get<double>(), c.at(2).get<double>(),
                                                                           c.at(3).get<double>()});
                }
                p.faceColors = geometry::FaceColors::fromIndices(p.shape, faces);
            }
            p.path = part.value("path", std::vector<std::string>());
            f.parts.push_back(std::move(p));
        }
        return f;
    }
    throw std::runtime_error("Unknown feature type " + type);
}

json parseDesign(const std::string& text)
{
    json root = json::parse(text);
    if (root.value("format", std::string()) != "kalve137-design") {
        throw std::runtime_error("Not a Kalve 137 design file");
    }
    const int version = root.value("version", 0);
    if (version > kDesignFormatVersion) {
        throw std::runtime_error("The design was saved by a newer Kalve 137 (format version " +
                                 std::to_string(version) + ")");
    }
    return root;
}

} // namespace

std::string toJson(const Document& document, ShapeStore& shapes)
{
    json features = json::array();
    for (const Feature& feature : document.features()) {
        features.push_back({{"id", feature.id},
                            {"type", typeName(feature.data)},
                            {"name", feature.name},
                            {"suppressed", feature.suppressed},
                            {"data", dataToJson(feature.data, shapes)}});
    }
    const json root = {
        {"format", "kalve137-design"}, {"version", kDesignFormatVersion}, {"units", "mm"},
        {"marker", document.marker()}, {"next_id", document.nextId()},    {"counters", document.counters()},
        {"features", features}};
    return root.dump(2) + "\n";
}

Document fromJson(const std::string& text, ShapeStore& shapes)
{
    try {
        const json root = parseDesign(text);
        std::vector<Feature> features;
        std::set<FeatureId> ids;
        for (const json& f : root.at("features")) {
            Feature feature;
            feature.id = f.at("id").get<int>();
            if (feature.id <= 0 || !ids.insert(feature.id).second) {
                throw std::runtime_error("The design file is damaged: feature id " + std::to_string(feature.id) +
                                         " is not unique");
            }
            feature.name = f.at("name").get<std::string>();
            feature.suppressed = f.value("suppressed", false);
            feature.data = dataFrom(f.at("type").get<std::string>(), f.at("data"), shapes);
            features.push_back(std::move(feature));
        }
        const std::size_t marker = root.value("marker", features.size());
        Document document;
        document.restore(std::move(features), marker, root.value("counters", std::map<std::string, int>()),
                         root.value("next_id", 1));
        return document;
    } catch (const json::exception& error) {
        throw std::runtime_error(std::string("The design file is damaged: ") + error.what());
    }
}

void writeFileAtomically(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::path temp = path;
    temp += ".saving";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Could not write " + utf8(path));
        }
        out << contents;
        out.flush();
        if (!out) {
            throw std::runtime_error("Could not write " + utf8(path));
        }
    }
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::filesystem::remove(temp, error);
        throw std::runtime_error("Could not replace " + utf8(path));
    }
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Could not open " + utf8(path));
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void saveDesign(const Document& document, const std::filesystem::path& path, ShapeStore& shapes)
{
    const std::size_t writesBefore = shapes.writes();
    const std::string text = toJson(document, shapes);
    writeFileAtomically(path, text);
    K137_INFO(Io, "Saved {} features ({} bytes, {} new shapes) to {}", document.features().size(), text.size(),
              shapes.writes() - writesBefore, utf8(path));
}

Document loadDesign(const std::filesystem::path& path, ShapeStore& shapes)
{
    const std::string text = readFile(path);
    Document document = fromJson(text, shapes);
    K137_INFO(Io, "Loaded {} features ({} bytes) from {}", document.features().size(), text.size(), utf8(path));
    return document;
}

DesignInfo peekDesign(const std::filesystem::path& path)
{
    try {
        const json root = parseDesign(readFile(path));
        DesignInfo info;
        for (const json& f : root.at("features")) {
            ++info.features;
            if (f.at("type") != "import") {
                continue;
            }
            for (const json& part : f.at("data").value("parts", json::array())) {
                if (part.contains("shape")) {
                    info.shapes.insert(part.at("shape").get<std::string>());
                }
            }
        }
        return info;
    } catch (const json::exception& error) {
        throw std::runtime_error(std::string("The design file is damaged: ") + error.what());
    }
}

} // namespace kalve137::core
