// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/session/design_session.h"
#include "app/view/pick.h"
#include "app/view/scene.h"
#include "core/naming.h"
#include "core/projection.h"
#include "geometry/modeling.h"
#include "log/log.h"
#include "render/rhi/viewport.h"
#include "viewer/occt_view_widget.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>

#include <QElapsedTimer>
#include <QScopedValueRollback>

#include <algorithm>
#include <optional>
#include <set>
#include <utility>
#include <variant>
#include <vector>

namespace kalve137::app {

namespace {

constexpr double kParallelAngle = 1e-6;

enum class Refusal { NotModel, Hidden, Later, Shape, Slanted, Duplicate };

struct Picked {
    core::ProjectionKind kind = core::ProjectionKind::Edge;
    core::ProjectedEdge projected;
    core::TopoRef ref;
    TopoDS_Shape shape;
};

struct PickContext {
    const DesignSession& session;
    const core::Model& before;
    gp_Ax3 plane;
};

QString textOf(Refusal refusal)
{
    switch (refusal) {
    case Refusal::NotModel:
        return SketchEditor::tr("Click an edge, a vertex or a face of a body");
    case Refusal::Hidden:
        return SketchEditor::tr("Hidden bodies don't project, show the body first");
    case Refusal::Later:
        return SketchEditor::tr("Only geometry built before this sketch projects into it");
    case Refusal::Shape:
        return SketchEditor::tr("Only straight edges and circles parallel to the sketch project for now");
    case Refusal::Slanted:
        return SketchEditor::tr("Only flat faces parallel to the sketch project");
    case Refusal::Duplicate:
        return SketchEditor::tr("That is already projected");
    }
    return {};
}

gp_Pnt midpointOf(const TopoDS_Edge& edge)
{
    const BRepAdaptor_Curve curve(edge);
    return curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2.0);
}

std::optional<TopoDS_Edge> circleThrough(const core::Body& body, const TopoDS_Vertex& vertex, const gp_Ax3& plane)
{
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> edges;
    TopExp::MapShapesAndAncestors(body.shape, TopAbs_VERTEX, TopAbs_EDGE, edges);
    const int index = edges.FindIndex(vertex);
    if (index == 0) {
        return std::nullopt;
    }
    for (const TopoDS_Shape& shape : edges.FindFromIndex(index)) {
        const TopoDS_Edge& edge = TopoDS::Edge(shape);
        const BRepAdaptor_Curve curve(edge);
        if (curve.GetType() == GeomAbs_Circle && BRep_Tool::IsClosed(edge) && core::projectEdge(edge, plane)) {
            return edge;
        }
    }
    return std::nullopt;
}

std::optional<TopoDS_Edge> seamCircle(const core::Body& body, const TopoDS_Edge& seam, const gp_Pnt& click,
                                      const gp_Ax3& plane)
{
    TopoDS_Vertex first;
    TopoDS_Vertex last;
    TopExp::Vertices(seam, first, last);
    if (first.IsNull() || last.IsNull()) {
        return std::nullopt;
    }
    const bool firstNearer = BRep_Tool::Pnt(first).Distance(click) <= BRep_Tool::Pnt(last).Distance(click);
    return circleThrough(body, firstNearer ? first : last, plane);
}

std::optional<TopoDS_Vertex> vertexAt(const TopoDS_Shape& shape, const gp_Pnt& at)
{
    for (TopExp_Explorer it(shape, TopAbs_VERTEX); it.More(); it.Next()) {
        const TopoDS_Vertex& vertex = TopoDS::Vertex(it.Current());
        if (BRep_Tool::Pnt(vertex).Distance(at) <= core::kRefTolerance) {
            return vertex;
        }
    }
    return std::nullopt;
}

std::variant<Refusal, Picked> edgeItem(const core::Body& body, const TopoDS_Edge& edge, const gp_Pnt& at,
                                       const gp_Ax3& plane)
{
    const std::optional<core::ProjectedEdge> projected = core::projectEdge(edge, plane);
    if (!projected) {
        return Refusal::Shape;
    }
    return Picked{core::ProjectionKind::Edge, *projected, core::edgeRef(body, edge, at), edge};
}

std::variant<Refusal, std::vector<Picked>> resolvePick(const PickContext& context, const core::Body& body,
                                                       const Pick& pick)
{
    std::vector<Picked> items;
    const auto add = [&](std::variant<Refusal, Picked> item) -> std::optional<Refusal> {
        if (const Refusal* refusal = std::get_if<Refusal>(&item)) {
            return *refusal;
        }
        items.push_back(std::get<Picked>(std::move(item)));
        return std::nullopt;
    };
    if (pick.kind == Pick::Kind::Vertex) {
        const std::optional<TopoDS_Vertex> vertex = vertexAt(body.shape, BRep_Tool::Pnt(TopoDS::Vertex(pick.shape)));
        if (!vertex) {
            return Refusal::Later;
        }
        if (const std::optional<TopoDS_Edge> circle = circleThrough(body, *vertex, context.plane)) {
            if (const std::optional<Refusal> refusal =
                    add(edgeItem(body, *circle, midpointOf(*circle), context.plane))) {
                return *refusal;
            }
            return items;
        }
        const std::optional<sketch::Vec2> at = core::projectVertex(*vertex, context.plane);
        if (!at) {
            return Refusal::Shape;
        }
        items.push_back({core::ProjectionKind::Vertex,
                         {core::ProjectedShape::Point, {*at}, 0.0},
                         core::vertexRef(body, *vertex),
                         *vertex});
        return items;
    }
    if (pick.kind == Pick::Kind::Edge) {
        const gp_Pnt at = midpointOf(TopoDS::Edge(pick.shape));
        std::optional<TopoDS_Edge> edge = geometry::findEdge(body.shape, at, core::kRefTolerance);
        if (!edge) {
            return Refusal::Later;
        }
        if (const std::optional<core::ProjectedEdge> flat = core::projectEdge(*edge, context.plane);
            flat && flat->shape == core::ProjectedShape::Point) {
            if (std::optional<TopoDS_Edge> circle = seamCircle(body, *edge, pick.point.value_or(at), context.plane)) {
                edge = std::move(circle);
            }
        }
        if (const std::optional<Refusal> refusal = add(edgeItem(body, *edge, midpointOf(*edge), context.plane))) {
            return *refusal;
        }
        return items;
    }
    const std::optional<TopoDS_Face> face =
        geometry::findFace(body.shape, geometry::facePoint(TopoDS::Face(pick.shape)), core::kRefTolerance);
    if (!face) {
        return Refusal::Later;
    }
    const std::optional<gp_Ax3> frame = geometry::planarFrame(*face);
    if (!frame || !frame->Direction().IsParallel(context.plane.Direction(), kParallelAngle)) {
        return Refusal::Slanted;
    }
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> loops;
    TopExp::MapShapes(*face, TopAbs_EDGE, loops);
    for (const TopoDS_Shape& shape : loops) {
        const TopoDS_Edge& edge = TopoDS::Edge(shape);
        if (BRep_Tool::Degenerated(edge)) {
            continue;
        }
        if (const std::optional<Refusal> refusal = add(edgeItem(body, edge, midpointOf(edge), context.plane))) {
            return *refusal;
        }
    }
    return items;
}

std::variant<Refusal, std::vector<Picked>> picked(const PickContext& context, const Pick& pick)
{
    if (pick.kind != Pick::Kind::Edge && pick.kind != Pick::Kind::Vertex && pick.kind != Pick::Kind::Face) {
        return Refusal::NotModel;
    }
    if (pick.shape.IsNull()) {
        return Refusal::NotModel;
    }
    if (!context.session.isBodyVisible(pick.body())) {
        return Refusal::Hidden;
    }
    const core::Body* body = context.before.body(pick.body());
    if (body == nullptr) {
        return Refusal::Later;
    }
    return resolvePick(context, *body, pick);
}

bool alreadyLinked(const core::Model& before, const core::SketchFeature& data, const Picked& item)
{
    return std::ranges::any_of(data.projections, [&](const core::Projection& link) {
        if (link.kind != item.kind) {
            return false;
        }
        if (link.kind == core::ProjectionKind::Vertex) {
            const std::optional<core::VertexHit> hit = core::resolveVertex(before.bodies, link.ref);
            return hit && hit->vertex.IsSame(item.shape);
        }
        const std::optional<core::EdgeHit> hit = core::resolveEdge(before.bodies, link.ref);
        return hit && hit->edge.IsSame(item.shape);
    });
}

void unproject(sketch::Sketch& s, const core::Projection& link)
{
    if (sketch::Curve* curve = s.curve(link.curve)) {
        curve->projected = false;
        for (const sketch::Id id : curve->points) {
            s.point(id)->projected = false;
        }
    }
    if (sketch::Point* point = s.point(link.point)) {
        point->projected = false;
    }
}

} // namespace

void SketchEditor::setModelPicking(bool picking)
{
    if (m_viewport != nullptr) {
        m_viewport->setLeftButtonCaptured(!picking);
    } else {
        m_view->setLeftButtonCaptured(!picking);
    }
    if (picking) {
        modelBefore();
    }
}

const core::Model& SketchEditor::modelBefore()
{
    const core::Document& shown = m_scene.document();
    const int index = shown.indexOf(m_sketchId);
    if (m_scene.editingSketch() == m_sketchId && index >= 0 && static_cast<std::size_t>(index) + 1 >= shown.marker()) {
        return m_scene.model();
    }
    if (!m_before) {
        QElapsedTimer timer;
        timer.start();
        m_before = core::modelBefore(m_session.document(), m_session.model(), m_sketchId);
        K137_INFO(App, "Project built the model before the sketch in {} ms", timer.elapsed());
    }
    return *m_before;
}

void SketchEditor::projectPick(const Pick& pick)
{
    if (!picksModel()) {
        return;
    }
    const std::optional<QString> refused = m_repick != 0 ? repick(pick) : project(pick);
    if (refused) {
        emit message(*refused);
    }
}

std::optional<QString> SketchEditor::project(const Pick& pick)
{
    const PickContext context{m_session, modelBefore(), plane()};
    std::variant<Refusal, std::vector<Picked>> resolved = picked(context, pick);
    if (const Refusal* refusal = std::get_if<Refusal>(&resolved)) {
        return textOf(*refusal);
    }
    std::vector<Picked>& items = std::get<std::vector<Picked>>(resolved);
    core::SketchFeature data = *feature();
    data.sketch = current();
    std::erase_if(items, [&](const Picked& item) { return alreadyLinked(context.before, data, item); });
    if (items.empty()) {
        return textOf(Refusal::Duplicate);
    }
    for (Picked& item : items) {
        if (item.kind == core::ProjectionKind::Vertex) {
            core::addProjectedVertex(data, item.projected.points.front(), std::move(item.ref));
        } else {
            core::addProjectedEdge(data, item.projected, std::move(item.ref));
        }
    }
    commitFeature(tr("Project"), std::move(data));
    return std::nullopt;
}

std::optional<QString> SketchEditor::repick(const Pick& pick)
{
    if (pick.kind == Pick::Kind::Face) {
        return tr("Click one edge or vertex for the selected curve");
    }
    const PickContext context{m_session, modelBefore(), plane()};
    std::variant<Refusal, std::vector<Picked>> resolved = picked(context, pick);
    if (const Refusal* refusal = std::get_if<Refusal>(&resolved)) {
        return textOf(*refusal);
    }
    std::vector<Picked>& items = std::get<std::vector<Picked>>(resolved);
    core::SketchFeature data = *feature();
    data.sketch = current();
    const auto link = std::ranges::find_if(data.projections, [&](const core::Projection& candidate) {
        return (candidate.curve != 0 ? candidate.curve : candidate.point) == m_repick;
    });
    if (link == data.projections.end() || items.size() != 1) {
        setTool(Tool::Select);
        return tr("The selected curve has no model link anymore");
    }
    Picked& item = items.front();
    if (!core::placeProjection(data.sketch, *link, item.projected)) {
        return tr("Pick a line for a line, a circle for a circle or an arc for an arc");
    }
    link->kind = item.kind;
    link->ref = std::move(item.ref);
    commitFeature(tr("Pick Again"), std::move(data));
    setTool(Tool::Select);
    return std::nullopt;
}

void SketchEditor::commitFeature(const QString& label, core::SketchFeature data)
{
    const core::FeatureId id = m_sketchId;
    const QScopedValueRollback committing(m_committing, true);
    m_session.edit(label, [id, &data](core::Document& document) {
        if (core::Feature* f = document.find(id)) {
            auto& target = std::get<core::SketchFeature>(f->data);
            target.sketch = std::move(data.sketch);
            target.projections = std::move(data.projections);
            core::dropBrokenProjections(target);
        }
    });
}

std::vector<std::size_t> SketchEditor::linksOf(const std::set<sketch::Id>& ids) const
{
    std::vector<std::size_t> found;
    const core::SketchFeature* data = feature();
    if (data == nullptr) {
        return found;
    }
    for (std::size_t i = 0; i < data->projections.size(); ++i) {
        const core::Projection& link = data->projections[i];
        if (ids.contains(link.curve != 0 ? link.curve : link.point)) {
            found.push_back(i);
        }
    }
    return found;
}

std::set<sketch::Id> SketchEditor::lostProjections() const
{
    const auto state = m_session.model().sketches.find(m_sketchId);
    if (state == m_session.model().sketches.end()) {
        return {};
    }
    std::set<sketch::Id> lost;
    for (const sketch::Id id : state->second.lostProjections) {
        lost.insert(id);
        if (const sketch::Curve* curve = m_current.curve(id)) {
            lost.insert(curve->points.begin(), curve->points.end());
        }
    }
    return lost;
}

void SketchEditor::breakLinks()
{
    if (!isActive()) {
        return;
    }
    const std::vector<std::size_t> links = linksOf(m_selection);
    if (links.empty()) {
        emit message(tr("Select projected curves to break their link"));
        return;
    }
    core::SketchFeature data = *feature();
    data.sketch = current();
    for (const std::size_t index : links) {
        unproject(data.sketch, data.projections[index]);
    }
    commitFeature(tr("Break Link"), std::move(data));
}

void SketchEditor::pickAgain()
{
    if (!isActive()) {
        return;
    }
    const std::vector<std::size_t> links = linksOf(m_selection);
    if (links.size() != 1) {
        emit message(tr("Select one projected curve to pick its edge again"));
        return;
    }
    const core::Projection& link = feature()->projections[links.front()];
    const sketch::Id target = link.curve != 0 ? link.curve : link.point;
    setTool(Tool::Project);
    m_repick = target;
    emit toolChanged();
}

} // namespace kalve137::app
