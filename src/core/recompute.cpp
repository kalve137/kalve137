// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/recompute.h"

#include "core/naming.h"

#include "geometry/modeling.h"
#include "geometry/shapes.h"
#include "geometry/topology.h"
#include "log/log.h"
#include "sketch/profiles.h"

#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepTools_History.hxx>
#include <Bnd_Box.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::core {

namespace {

constexpr double kTouchTolerance = 1e-6;
constexpr double kMinPatternSpacing = 1e-6;
constexpr double kParallelAngle = 1e-6;
constexpr double kThroughAllMargin = 0.01;

struct FeatureError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void reshape(Body& body, const TopoDS_Shape& shape, const BRepTools_History& history, FeatureId id,
             const TopoDS_Shape& tool = {}, const geometry::FaceNames* toolNames = nullptr)
{
    if (!body.faceColors.empty()) {
        body.faceColors.remap(body.shape, history);
        body.faceColors.prune(shape);
    }
    if (toolNames != nullptr) {
        body.faceNames.merge(*toolNames);
    }
    const std::string prefix = namePrefix(id);
    body.faceNames.remap({body.shape, tool}, history, prefix);
    body.faceNames.prune(shape);
    body.faceNames.nameRest(shape, prefix);
    body.shape = shape;
}

bool touches(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    BRepExtrema_DistShapeShape distance(a, b);
    return distance.IsDone() && distance.Value() <= kTouchTolerance;
}

class Builder {
public:
    explicit Builder(Model& model) : m_model(model) {}

    void build(const Feature& feature)
    {
        std::visit([&](const auto& data) { buildFeature(feature, data); }, feature.data);
    }

private:
    void buildFeature(const Feature& feature, const SketchFeature& data)
    {
        SketchState state;
        state.plane = data.plane;
        if (data.planeFeature != 0) {
            auto plane = m_model.planes.find(data.planeFeature);
            if (plane == m_model.planes.end()) {
                throw FeatureError("The plane this sketch is on is missing or rolled back");
            }
            state.plane = plane->second.frame;
        }
        bool faceLost = false;
        if (data.face) {
            const std::optional<FaceHit> hit = resolveFace(m_model.bodies, *data.face);
            const std::optional<gp_Ax3> frame = hit ? geometry::planarFrame(hit->face) : std::nullopt;
            if (frame) {
                state.plane = *frame;
            } else {
                faceLost = true;
            }
        }
        state.solved = data.sketch;
        state.solve = sketch::solve(state.solved);
        state.profiles = sketch::findProfiles(state.solved, state.plane);
        m_model.sketches.insert_or_assign(feature.id, std::move(state));
        if (faceLost) {
            warn(feature.id, "The face this sketch is on is gone, so the sketch stays where it was");
        } else if (!m_model.sketches.at(feature.id).solve.conflicting.empty()) {
            warn(feature.id, "The sketch has conflicting constraints");
        }
    }

    void buildFeature(const Feature& feature, const ExtrudeFeature& data)
    {
        const SketchState& state = sketchState(data.profiles.sketch);
        const std::vector<TopoDS_Face> faces = resolveProfiles(state, data.profiles);
        const gp_Vec normal(state.plane.Direction());
        gp_Vec from(0.0, 0.0, 0.0);
        gp_Vec to = normal * data.distance;
        switch (data.end) {
        case ExtrudeEnd::Distance:
            switch (data.extent) {
            case ExtrudeExtent::OneSide:
                break;
            case ExtrudeExtent::Symmetric:
                from = to * -0.5;
                to *= 0.5;
                break;
            case ExtrudeExtent::TwoSides: {
                const double side = data.distance < 0.0 ? 1.0 : -1.0;
                from = normal * (side * data.secondDistance);
                break;
            }
            }
            break;
        case ExtrudeEnd::ToObject:
            to = normal * distanceToObject(data, state.plane);
            break;
        case ExtrudeEnd::All: {
            const auto [below, above] = spanAlong(state.plane, faces);
            if (data.extent == ExtrudeExtent::OneSide) {
                to = normal * (data.distance < 0.0 ? below : above);
            } else {
                from = normal * below;
                to = normal * above;
            }
            break;
        }
        }
        const TopoDS_Shape tool = geometry::extrude(faces, from, to, data.taperDegrees * std::numbers::pi / 180.0);
        applyOperation(feature.id, data.bodyName, data.operation, tool,
                       nameExtrusion(tool, feature.id, state, faces, from, to));
    }

    double distanceToObject(const ExtrudeFeature& data, const gp_Ax3& sketchPlane) const
    {
        std::optional<gp_Ax3> target;
        if (data.toPlane != 0) {
            const auto plane = m_model.planes.find(data.toPlane);
            if (plane == m_model.planes.end()) {
                throw FeatureError("The plane this extrude goes to is missing or rolled back");
            }
            target = plane->second.frame;
        } else if (data.toFace) {
            const std::optional<FaceHit> hit = resolveFace(m_model.bodies, *data.toFace);
            if (!hit) {
                throw FeatureError("The face this extrude goes to is gone");
            }
            target = geometry::planarFrame(hit->face);
            if (!target) {
                throw FeatureError("Extrude To Object needs a flat face or a plane");
            }
        } else {
            throw FeatureError("No face or plane selected to extrude to");
        }
        if (!target->Direction().IsParallel(sketchPlane.Direction(), kParallelAngle)) {
            throw FeatureError("Extrude To Object needs a face or plane parallel to the sketch");
        }
        const double distance = gp_Vec(sketchPlane.Location(), target->Location()).Dot(gp_Vec(sketchPlane.Direction()));
        if (std::abs(distance) < kTouchTolerance) {
            throw FeatureError("The face or plane to extrude to is on the sketch plane");
        }
        return distance;
    }

    std::pair<double, double> spanAlong(const gp_Ax3& plane, const std::vector<TopoDS_Face>& faces) const
    {
        Bnd_Box box;
        for (const Body& body : m_model.bodies) {
            BRepBndLib::Add(body.shape, box);
        }
        for (const TopoDS_Face& face : faces) {
            BRepBndLib::Add(face, box);
        }
        if (box.IsVoid()) {
            throw FeatureError("There is nothing to extrude through");
        }
        double xmin = 0.0;
        double ymin = 0.0;
        double zmin = 0.0;
        double xmax = 0.0;
        double ymax = 0.0;
        double zmax = 0.0;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const gp_Vec normal(plane.Direction());
        double below = 0.0;
        double above = 0.0;
        for (const double x : {xmin, xmax}) {
            for (const double y : {ymin, ymax}) {
                for (const double z : {zmin, zmax}) {
                    const double height = gp_Vec(plane.Location(), gp_Pnt(x, y, z)).Dot(normal);
                    below = std::min(below, height);
                    above = std::max(above, height);
                }
            }
        }
        const double margin = std::max(1.0, (above - below) * kThroughAllMargin);
        return {below - margin, above + margin};
    }

    void buildFeature(const Feature& feature, const RevolveFeature& data)
    {
        const SketchState& state = sketchState(data.profiles.sketch);
        const std::vector<TopoDS_Face> faces = resolveProfiles(state, data.profiles);
        const gp_Ax1 axis = resolveAxis(data.axis);
        const double angle = std::clamp(data.angleDegrees, 0.0, 360.0) * std::numbers::pi / 180.0;
        const TopoDS_Shape tool = geometry::revolve(faces, axis, angle);
        applyOperation(feature.id, data.bodyName, data.operation, tool,
                       nameRevolution(tool, feature.id, state, faces, axis));
    }

    void buildFeature(const Feature& feature, const FilletFeature& data)
    {
        std::vector<TopoRef> refs;
        std::vector<double> radii;
        for (const FilletSet& set : data.sets) {
            refs.insert(refs.end(), set.edges.begin(), set.edges.end());
            radii.insert(radii.end(), set.edges.size(), set.radius);
        }
        modifyEdges(feature.id, refs, radii,
                    [&](const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges,
                        const std::vector<double>& sizes,
                        BRepTools_History* history) { return geometry::fillet(shape, edges, sizes, history); });
    }

    void buildFeature(const Feature& feature, const ChamferFeature& data)
    {
        modifyEdges(
            feature.id, data.edges, std::vector<double>(data.edges.size(), data.distance),
            [&](const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges, const std::vector<double>&,
                BRepTools_History* history) { return geometry::chamfer(shape, edges, data.distance, history); });
    }

    void buildFeature(const Feature& feature, const ImportFeature& data)
    {
        if (data.parts.empty() || std::ranges::any_of(data.parts, [](const auto& p) { return p.shape.IsNull(); })) {
            throw FeatureError("The imported shape is missing");
        }
        for (std::size_t i = 0; i < data.parts.size(); ++i) {
            const geometry::StepPart& part = data.parts[i];
            m_model.bodies.push_back({feature.id, part.name, part.shape, part.color, part.faceColors,
                                      static_cast<int>(i), part.path,
                                      nameByIndex(part.shape, namePrefix(feature.id) + "/part" + std::to_string(i))});
        }
    }

    void buildFeature(const Feature& feature, const PlaneFeature& data)
    {
        if (data.baseName.empty()) {
            throw FeatureError(data.kind == PlaneKind::Angle ? "No line selected" : "No plane or face selected");
        }
        m_model.planes.insert_or_assign(feature.id, constructionPlane(data));
    }

    void buildFeature(const Feature& feature, const AxisFeature& data)
    {
        if (data.baseName.empty()) {
            throw FeatureError("No edge, face or points selected");
        }
        m_model.axes.insert_or_assign(feature.id, data.axis);
    }

    void buildFeature(const Feature& feature, const PointFeature& data)
    {
        if (data.baseName.empty()) {
            throw FeatureError("No vertex, edge or face selected");
        }
        m_model.points.insert_or_assign(feature.id, data.point);
    }

    void buildFeature(const Feature& feature, const ShellFeature& data)
    {
        modifyFaces(feature.id, data.faces,
                    [&](const TopoDS_Shape& shape, const std::vector<TopoDS_Face>& faces, BRepTools_History* history) {
                        return geometry::shell(shape, faces, data.thickness, data.inside, history);
                    });
    }

    void buildFeature(const Feature& feature, const OffsetFaceFeature& data)
    {
        modifyFaces(feature.id, data.faces,
                    [&](const TopoDS_Shape& shape, const std::vector<TopoDS_Face>& faces, BRepTools_History* history) {
                        return geometry::offsetFaces(shape, faces, data.distance, history);
                    });
    }

    void buildFeature(const Feature& feature, const HoleFeature& data)
    {
        std::vector<gp_Ax1> axes;
        std::size_t lost = 0;
        if (data.sketch != 0) {
            const SketchState& state = sketchState(data.sketch);
            const gp_Dir drill = data.flip ? state.plane.Direction() : state.plane.Direction().Reversed();
            for (const sketch::Id id : data.sketchPoints) {
                if (const sketch::Point* point = state.solved.point(id)) {
                    axes.emplace_back(sketch::toWorld(state.plane, point->pos), drill);
                } else {
                    ++lost;
                }
            }
        } else {
            for (const TopoRef& ref : data.facePoints) {
                if (const std::optional<gp_Ax1> axis = holeAxis(m_model.bodies, ref)) {
                    axes.push_back(*axis);
                } else {
                    ++lost;
                }
            }
        }
        if (axes.empty()) {
            throw FeatureError(lost > 0 ? "The hole positions no longer exist" : "No hole positions selected");
        }
        geometry::HoleParams params;
        switch (data.kind) {
        case HoleKind::Simple:
            params.head = geometry::HoleHead::None;
            break;
        case HoleKind::Counterbore:
            params.head = geometry::HoleHead::Counterbore;
            break;
        case HoleKind::Countersink:
            params.head = geometry::HoleHead::Countersink;
            break;
        }
        params.diameter = data.diameter;
        params.depth = data.throughAll ? throughDepth(axes) : data.depth;
        params.headDiameter = data.headDiameter;
        params.headDepth = data.headDepth;
        params.countersinkAngleDegrees = data.countersinkAngle;
        params.tipAngleDegrees = data.throughAll ? 0.0 : data.tipAngle;
        TopoDS_Shape tool = geometry::holeTool(axes.front(), params);
        for (std::size_t i = 1; i < axes.size(); ++i) {
            tool = geometry::fuse(tool, geometry::holeTool(axes[i], params));
        }
        const geometry::FaceNames toolNames = nameByIndex(tool, namePrefix(feature.id) + "/hole");
        bool any = false;
        for (Body& body : m_model.bodies) {
            if (geometry::overlaps(body.shape, tool)) {
                BRepTools_History history;
                reshape(body, geometry::cut(body.shape, tool, &history), history, feature.id, tool, &toolNames);
                any = true;
            }
        }
        if (!any) {
            throw FeatureError("The hole misses every body");
        }
        std::erase_if(m_model.bodies, [](const Body& body) { return geometry::isEmpty(body.shape); });
        if (lost > 0) {
            warn(feature.id, std::to_string(lost) + " of the hole positions no longer exist");
        }
    }

    void buildFeature(const Feature& feature, const MoveFeature& data)
    {
        if (data.bodies.empty()) {
            throw FeatureError("No bodies selected");
        }
        gp_Trsf rotation;
        rotation.SetRotation(gp_Ax1(data.pivot, worldAxis(data.rotateAxis)),
                             data.angleDegrees * std::numbers::pi / 180.0);
        gp_Trsf placement;
        placement.SetTranslation(data.translation);
        placement.Multiply(rotation);
        const TopLoc_Location location(placement);
        std::size_t lost = 0;
        int part = 0;
        for (std::size_t i = 0; i < data.bodies.size(); ++i) {
            auto body = std::ranges::find(m_model.bodies, data.bodies[i], &Body::key);
            if (body == m_model.bodies.end()) {
                ++lost;
                continue;
            }
            if (!data.copy) {
                body->shape = body->shape.Moved(location);
                continue;
            }
            Body copy = *body;
            copy.id = feature.id;
            copy.part = part++;
            if (i < data.copyNames.size()) {
                copy.name = data.copyNames[i];
            }
            copy.shape = body->shape.Moved(location);
            m_model.bodies.push_back(std::move(copy));
        }
        if (lost == data.bodies.size()) {
            throw FeatureError("The bodies this move uses no longer exist");
        }
        if (lost > 0) {
            warn(feature.id, std::to_string(lost) + " of the selected bodies no longer exist");
        }
    }

    void buildFeature(const Feature& feature, const AppearanceFeature& data)
    {
        if (data.bodies.empty() && data.faces.empty()) {
            throw FeatureError("No bodies or faces selected");
        }
        if (!data.color && !data.clear) {
            throw FeatureError("No color chosen");
        }
        std::size_t lost = 0;
        for (const BodyKey& key : data.bodies) {
            auto body = std::ranges::find(m_model.bodies, key, &Body::key);
            if (body == m_model.bodies.end()) {
                ++lost;
                continue;
            }
            body->color = data.color;
            body->faceColors.clear();
        }
        for (const TopoRef& ref : data.faces) {
            const std::optional<FaceHit> hit = resolveFace(m_model.bodies, ref);
            if (!hit) {
                ++lost;
                continue;
            }
            Body& body = m_model.bodies[hit->body];
            if (data.color) {
                body.faceColors.set(hit->face, *data.color);
            } else {
                body.faceColors.clear(hit->face);
            }
        }
        if (lost == data.bodies.size() + data.faces.size()) {
            throw FeatureError("The bodies and faces this feature colors no longer exist");
        }
        if (lost > 0) {
            warn(feature.id, std::to_string(lost) + " of the selected bodies and faces no longer exist");
        }
    }

    void buildFeature(const Feature& feature, const PatternFeature& data)
    {
        if (data.count < kMinPatternCount) {
            throw FeatureError("A pattern needs at least two copies");
        }
        if (data.count > kMaxPatternCount) {
            throw FeatureError("A pattern makes at most " + std::to_string(kMaxPatternCount) + " copies");
        }
        auto body = std::ranges::find(m_model.bodies, data.body, &Body::key);
        if (data.body.feature == 0 || body == m_model.bodies.end()) {
            throw FeatureError(data.body.feature == 0 ? "No body selected"
                                                      : "The body this pattern copies no longer exists");
        }
        if (data.kind == PatternKind::Linear && std::abs(data.spacing) < kMinPatternSpacing) {
            throw FeatureError("The spacing is zero, so every copy lands on the original");
        }
        const gp_Ax1 axis = resolveAxis(data.axis);
        const bool fullTurn = std::abs(std::abs(data.angleDegrees) - 360.0) < 1e-9;
        const double stepDegrees = data.angleDegrees / (fullTurn ? data.count : data.count - 1);
        std::vector<gp_Trsf> placements;
        for (int i = 1; i < data.count; ++i) {
            gp_Trsf placement;
            switch (data.kind) {
            case PatternKind::Linear:
                placement.SetTranslation(gp_Vec(axis.Direction()) * (data.spacing * i));
                break;
            case PatternKind::Circular:
                placement.SetRotation(axis, stepDegrees * i * std::numbers::pi / 180.0);
                break;
            }
            placements.push_back(placement);
        }
        BRepTools_History history;
        reshape(*body, geometry::pattern(body->shape, placements, &history), history, feature.id);
    }

    const SketchState& sketchState(FeatureId id) const
    {
        auto it = m_model.sketches.find(id);
        if (it == m_model.sketches.end()) {
            throw FeatureError("The sketch this feature uses is missing or rolled back");
        }
        return it->second;
    }

    static std::vector<TopoDS_Face> resolveProfiles(const SketchState& state, const ProfileRef& ref)
    {
        if (ref.seeds.empty()) {
            throw FeatureError("No profiles selected");
        }
        std::vector<TopoDS_Face> faces;
        std::vector<std::size_t> used;
        for (const sketch::Vec2& seed : ref.seeds) {
            const auto index = sketch::profileAt(state.profiles, state.plane, seed);
            if (!index) {
                throw FeatureError("A profile this feature uses no longer exists");
            }
            if (std::ranges::find(used, *index) == used.end()) {
                used.push_back(*index);
                faces.push_back(state.profiles[*index]);
            }
        }
        return faces;
    }

    gp_Ax1 resolveAxis(const AxisRef& axis) const
    {
        switch (axis.kind) {
        case AxisKind::X:
        case AxisKind::Y:
        case AxisKind::Z:
            return gp_Ax1(gp::Origin(), worldAxis(axis.kind));
        case AxisKind::SketchLine:
            return sketchLineAxis(axis);
        case AxisKind::Construction: {
            auto it = m_model.axes.find(axis.construction);
            if (it == m_model.axes.end()) {
                throw FeatureError("The construction axis is missing or rolled back");
            }
            return it->second;
        }
        }
        throw FeatureError("Unknown axis");
    }

    static gp_Dir worldAxis(AxisKind kind)
    {
        switch (kind) {
        case AxisKind::X:
            return gp::DX();
        case AxisKind::Y:
            return gp::DY();
        case AxisKind::Z:
            return gp::DZ();
        case AxisKind::SketchLine:
        case AxisKind::Construction:
            break;
        }
        throw FeatureError("Only the X, Y and Z axes work here");
    }

    double throughDepth(const std::vector<gp_Ax1>& axes) const
    {
        Bnd_Box box;
        for (const Body& body : m_model.bodies) {
            BRepBndLib::Add(body.shape, box);
        }
        if (box.IsVoid()) {
            throw FeatureError("There are no bodies to drill through");
        }
        const double diagonal = std::sqrt(box.SquareExtent());
        const gp_Pnt center((box.CornerMin().XYZ() + box.CornerMax().XYZ()) * 0.5);
        double farthest = 0.0;
        for (const gp_Ax1& axis : axes) {
            farthest = std::max(farthest, axis.Location().Distance(center));
        }
        return farthest + diagonal;
    }

    gp_Ax1 sketchLineAxis(const AxisRef& axis) const
    {
        const SketchState& state = sketchState(axis.sketch);
        const sketch::Sketch* source = &state.solved;
        const sketch::Curve* line = source->curve(axis.line);
        if (line == nullptr || line->kind != sketch::CurveKind::Line) {
            throw FeatureError("The axis line no longer exists");
        }
        const gp_Pnt a = sketch::toWorld(state.plane, source->point(line->points[0])->pos);
        const gp_Pnt b = sketch::toWorld(state.plane, source->point(line->points[1])->pos);
        if (a.Distance(b) < 1e-9) {
            throw FeatureError("The axis line has no length");
        }
        return gp_Ax1(a, gp_Dir(gp_Vec(a, b)));
    }

    void applyOperation(FeatureId id, const std::string& bodyName, Operation op, const TopoDS_Shape& tool,
                        const geometry::FaceNames& toolNames)
    {
        auto& bodies = m_model.bodies;
        const auto addBody = [&] {
            Body body{id, bodyName, tool};
            body.faceNames = toolNames;
            bodies.push_back(std::move(body));
        };
        switch (op) {
        case Operation::NewBody:
            addBody();
            return;
        case Operation::Join: {
            std::vector<std::size_t> targets;
            for (std::size_t i = 0; i < bodies.size(); ++i) {
                if (touches(bodies[i].shape, tool)) {
                    targets.push_back(i);
                }
            }
            if (targets.empty()) {
                addBody();
                return;
            }
            Body& first = bodies[targets.front()];
            {
                BRepTools_History history;
                reshape(first, geometry::fuse(first.shape, tool, &history), history, id, tool, &toolNames);
            }
            for (std::size_t i = targets.size(); i-- > 1;) {
                Body& absorbed = bodies[targets[i]];
                BRepTools_History history;
                const TopoDS_Shape fused = geometry::fuse(first.shape, absorbed.shape, &history);
                reshape(first, fused, history, id, absorbed.shape, &absorbed.faceNames);
                reshape(absorbed, fused, history, id);
                first.faceColors.merge(absorbed.faceColors);
                bodies.erase(bodies.begin() + static_cast<std::ptrdiff_t>(targets[i]));
            }
            return;
        }
        case Operation::Cut: {
            bool any = false;
            for (Body& body : bodies) {
                if (geometry::overlaps(body.shape, tool)) {
                    BRepTools_History history;
                    reshape(body, geometry::cut(body.shape, tool, &history), history, id, tool, &toolNames);
                    any = true;
                }
            }
            if (!any) {
                throw FeatureError("The cut does not intersect any body");
            }
            std::erase_if(bodies, [](const Body& body) { return geometry::isEmpty(body.shape); });
            return;
        }
        case Operation::Intersect: {
            bool any = false;
            for (Body& body : bodies) {
                if (geometry::overlaps(body.shape, tool)) {
                    BRepTools_History history;
                    reshape(body, geometry::common(body.shape, tool, &history), history, id, tool, &toolNames);
                    any = true;
                }
            }
            if (!any) {
                throw FeatureError("The intersection does not overlap any body");
            }
            std::erase_if(bodies, [](const Body& body) { return geometry::isEmpty(body.shape); });
            return;
        }
        }
    }

    template <typename Op>
    void modifyEdges(FeatureId id, const std::vector<TopoRef>& refs, const std::vector<double>& sizes, Op&& op)
    {
        if (refs.empty()) {
            throw FeatureError("No edges selected");
        }
        struct Picked {
            std::vector<TopoDS_Edge> edges;
            std::vector<double> sizes;
        };
        std::map<std::size_t, Picked> edgesByBody;
        std::size_t lost = 0;
        for (std::size_t i = 0; i < refs.size(); ++i) {
            const std::optional<EdgeHit> hit = resolveEdge(m_model.bodies, refs[i]);
            if (!hit) {
                ++lost;
                continue;
            }
            Picked& picked = edgesByBody[hit->body];
            if (std::ranges::none_of(picked.edges, [&](const TopoDS_Edge& edge) { return edge.IsSame(hit->edge); })) {
                picked.edges.push_back(hit->edge);
                picked.sizes.push_back(sizes.at(i));
            }
        }
        if (edgesByBody.empty()) {
            throw FeatureError("The edges this feature uses no longer exist");
        }
        for (auto& [index, picked] : edgesByBody) {
            Body& body = m_model.bodies[index];
            BRepTools_History history;
            reshape(body, op(body.shape, picked.edges, picked.sizes, &history), history, id);
        }
        if (lost > 0) {
            warn(id, std::to_string(lost) + " of the selected edges no longer exist");
        }
    }

    template <typename Op>
    void modifyFaces(FeatureId id, const std::vector<TopoRef>& refs, Op&& op)
    {
        if (refs.empty()) {
            throw FeatureError("No faces selected");
        }
        std::map<std::size_t, std::vector<TopoDS_Face>> facesByBody;
        std::size_t lost = 0;
        for (const TopoRef& ref : refs) {
            const std::optional<FaceHit> hit = resolveFace(m_model.bodies, ref);
            if (!hit) {
                ++lost;
                continue;
            }
            std::vector<TopoDS_Face>& faces = facesByBody[hit->body];
            if (std::ranges::none_of(faces, [&](const TopoDS_Face& face) { return face.IsSame(hit->face); })) {
                faces.push_back(hit->face);
            }
        }
        if (facesByBody.empty()) {
            throw FeatureError("The faces this feature uses no longer exist");
        }
        for (auto& [index, faces] : facesByBody) {
            Body& body = m_model.bodies[index];
            BRepTools_History history;
            reshape(body, op(body.shape, faces, &history), history, id);
        }
        if (lost > 0) {
            warn(id, std::to_string(lost) + " of the selected faces no longer exist");
        }
    }

    void warn(FeatureId id, std::string message) { m_model.status[id] = {FeatureHealth::Warning, std::move(message)}; }

    Model& m_model;
};

bool buildOne(Builder& builder, Model& model, const Feature& feature)
{
    if (feature.suppressed) {
        model.status[feature.id] = {FeatureHealth::Suppressed, {}};
        return false;
    }
    model.status[feature.id] = {FeatureHealth::Ok, {}};
    const auto start = std::chrono::steady_clock::now();
    std::vector<Body> bodiesBefore = model.bodies;
    std::string failure;
    try {
        builder.build(feature);
    } catch (const std::exception& error) {
        failure = error.what();
    } catch (...) {
        failure = "Unknown error";
    }
    const bool failed = !failure.empty();
    if (failed) {
        model.bodies = std::move(bodiesBefore);
        model.sketches.erase(feature.id);
        model.planes.erase(feature.id);
        model.axes.erase(feature.id);
        model.points.erase(feature.id);
        model.status[feature.id] = {FeatureHealth::Failed, failure};
        K137_DEBUG(Core, "{} ({}) failed: {}", feature.name, typeName(feature.data), failure);
    }
    K137_TRACE(Core, "{} ({}) built in {} ms", feature.name, typeName(feature.data),
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    return failed;
}

} // namespace

const Body* Model::body(BodyKey key) const
{
    auto it = std::ranges::find(bodies, key, &Body::key);
    return it == bodies.end() ? nullptr : &*it;
}

std::vector<TopoDS_Shape> Model::shapes() const
{
    std::vector<TopoDS_Shape> result;
    result.reserve(bodies.size());
    for (const Body& body : bodies) {
        result.push_back(body.shape);
    }
    return result;
}

std::vector<geometry::StepPart> Model::stepParts() const
{
    std::vector<geometry::StepPart> result;
    result.reserve(bodies.size());
    for (const Body& body : bodies) {
        result.push_back({body.name, body.shape, body.color, body.faceColors, body.path});
    }
    return result;
}

Model recompute(const Document& document)
{
    const auto start = std::chrono::steady_clock::now();
    Model model;
    Builder builder(model);
    const auto& features = document.features();
    std::size_t failed = 0;
    for (std::size_t i = 0; i < features.size(); ++i) {
        const Feature& feature = features[i];
        if (i >= document.marker()) {
            model.status[feature.id] = {FeatureHealth::RolledBack, {}};
            continue;
        }
        failed += buildOne(builder, model, feature) ? 1 : 0;
    }
    K137_DEBUG(Core, "Recomputed {} features, {} failed, {} bodies in {:.1f} ms", features.size(), failed,
               model.bodies.size(),
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    return model;
}

Model extend(const Model& base, const Feature& feature)
{
    Model model = base;
    Builder builder(model);
    buildOne(builder, model, feature);
    return model;
}

} // namespace kalve137::core
