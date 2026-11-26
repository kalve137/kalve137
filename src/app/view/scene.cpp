// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/scene.h"

#include "app/session/design_session.h"
#include "app/theme/theme.h"
#include "app/view/scene_detail.h"
#include "core/snapshot_cache.h"
#include "geometry/meshing.h"
#include "geometry/modeling.h"
#include "geometry/topology.h"

#include "log/log.h"
#include "pick/scene_rays.h"
#include "sketch/profiles.h"
#include "viewer/occt_view_widget.h"
#include "viewer/style.h"

#include <OcctQtTools.h>

#include <AIS_ColoredShape.hxx>
#include <AIS_Point.hxx>
#include <AIS_Shape.hxx>
#include <Geom_CartesianPoint.hxx>
#include <OSD_Parallel.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Standard_Failure.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <utility>
#include <variant>

namespace kalve137::app {

using scene_detail::hoverInOwnLayer;
using scene_detail::kNotSelectable;
using scene_detail::kWholeObject;
using scene_detail::makeSurface;
using scene_detail::makeWire;
using scene_detail::occt;

namespace {

constexpr double kHighlightEdgeWidth = viewer::style::kStrokeBold;
constexpr double kHighlightTransparency = viewer::style::kTintStrong;
constexpr double kBodyEdgeWidth = viewer::style::kStrokeThin;
constexpr double kWireWidth = viewer::style::kStrokeHairline;
constexpr double kFaceBoundaryWidth = viewer::style::kStrokeHairline;
constexpr double kSketchPointScale = viewer::style::kMarkerSmall;
constexpr double kPlanePickBodyTransparency = viewer::style::kTintHalf;
constexpr double kPreviewBodyTransparency = viewer::style::kTintHalf;
constexpr double kProfileTransparency = viewer::style::kTintFaint;

void paintFaces(const Handle(AIS_ColoredShape)& object, const core::Body& body)
{
    for (TopExp_Explorer it(body.shape, TopAbs_FACE); it.More(); it.Next()) {
        if (const auto color = body.faceColors.find(TopoDS::Face(it.Current()))) {
            object->SetCustomColor(it.Current(), occt(*color));
        }
    }
}

std::pair<Handle(AIS_ColoredShape), int> makeBody(const core::Body& body, VisualStyle style)
{
    Handle(AIS_ColoredShape) object = new AIS_ColoredShape(body.shape);
    const Quantity_Color surface = body.color ? occt(*body.color) : occt(theme::palette().viewport.body);
    int displayMode = AIS_Shaded;
    switch (style) {
    case VisualStyle::Shaded:
        object->SetColor(surface);
        paintFaces(object, body);
        break;
    case VisualStyle::ShadedWithEdges:
        object->SetColor(surface);
        paintFaces(object, body);
        object->Attributes()->SetFaceBoundaryDraw(true);
        object->Attributes()->SetFaceBoundaryAspect(
            new Prs3d_LineAspect(occt(theme::palette().viewport.bodyEdge), Aspect_TOL_SOLID, kBodyEdgeWidth));
        break;
    case VisualStyle::Wireframe:
        displayMode = AIS_WireFrame;
        object->SetColor(occt(theme::palette().viewport.wire));
        object->Attributes()->SetWireAspect(
            new Prs3d_LineAspect(occt(theme::palette().viewport.wire), Aspect_TOL_SOLID, kWireWidth));
        break;
    case VisualStyle::VisibleEdges:
        object->SetColor(occt(theme::palette().viewport.canvas));
        object->Attributes()->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
        object->Attributes()->SetFaceBoundaryDraw(true);
        object->Attributes()->SetFaceBoundaryAspect(
            new Prs3d_LineAspect(occt(theme::palette().viewport.wire), Aspect_TOL_SOLID, kBodyEdgeWidth));
        break;
    }
    return {object, displayMode};
}

const char* toString(PickMode mode)
{
    switch (mode) {
    case PickMode::None:
        return "none";
    case PickMode::Planes:
        return "planes";
    case PickMode::Profiles:
        return "profiles";
    case PickMode::ProfilesAndLines:
        return "profiles and lines";
    case PickMode::Edges:
        return "edges";
    case PickMode::Faces:
        return "faces";
    case PickMode::Measure:
        return "measure";
    case PickMode::Project:
        return "project";
    case PickMode::PressPull:
        return "press pull";
    case PickMode::HolePlacement:
        return "hole placement";
    case PickMode::Lines:
        return "lines";
    case PickMode::AxisSources:
        return "axis sources";
    case PickMode::PointSources:
        return "point sources";
    }
    return "none";
}

} // namespace

std::vector<TopAbs_ShapeEnum> bodySelection(PickMode mode, bool sketching)
{
    switch (mode) {
    case PickMode::None:
        if (sketching) {
            return {};
        }
        return {TopAbs_FACE, TopAbs_EDGE};
    case PickMode::Profiles:
    case PickMode::ProfilesAndLines:
        return {};
    case PickMode::Planes:
    case PickMode::Faces:
    case PickMode::HolePlacement:
        return {TopAbs_FACE};
    case PickMode::Edges:
    case PickMode::Lines:
        return {TopAbs_EDGE};
    case PickMode::PressPull:
        return {TopAbs_FACE, TopAbs_EDGE};
    case PickMode::Measure:
    case PickMode::Project:
    case PickMode::AxisSources:
    case PickMode::PointSources:
        return {TopAbs_VERTEX, TopAbs_EDGE, TopAbs_FACE};
    }
    return {};
}

bool planesPickable(PickMode mode)
{
    return mode == PickMode::Planes;
}

bool axesPickable(PickMode mode)
{
    return mode == PickMode::Lines || mode == PickMode::ProfilesAndLines || mode == PickMode::Faces;
}

Quantity_Color scene_detail::occt(const geometry::Color& color)
{
    return Quantity_Color(color.red, color.green, color.blue, Quantity_TOC_sRGB);
}

Quantity_Color scene_detail::occt(const QColor& color)
{
    return OcctQtTools::qtColorToOcct(color);
}

Handle(AIS_Shape) scene_detail::makeWire(const TopoDS_Shape& shape, const QColor& color, double width,
                                         Aspect_TypeOfLine type)
{
    Handle(AIS_Shape) object = new AIS_Shape(shape);
    object->SetColor(occt(color));
    object->Attributes()->SetWireAspect(new Prs3d_LineAspect(occt(color), type, width));
    object->SetWidth(width);
    return object;
}

Handle(AIS_Shape) scene_detail::makeSurface(const TopoDS_Shape& shape, const QColor& color, double transparency)
{
    Handle(AIS_Shape) object = new AIS_Shape(shape);
    object->SetColor(occt(color));
    object->SetTransparency(transparency);
    object->Attributes()->SetFaceBoundaryDraw(true);
    object->Attributes()->SetFaceBoundaryAspect(
        new Prs3d_LineAspect(occt(color), Aspect_TOL_SOLID, kFaceBoundaryWidth));
    return object;
}

void scene_detail::hoverInOwnLayer(const Handle(AIS_InteractiveObject)& object,
                                   const Handle(AIS_InteractiveContext)& context)
{
    const Handle(Prs3d_Drawer)& style = context->HighlightStyle(Prs3d_TypeOfHighlight_Dynamic);
    Handle(Prs3d_Drawer) hover = new Prs3d_Drawer();
    hover->SetMethod(style->Method());
    hover->SetColor(style->Color());
    hover->SetTransparency(style->Transparency());
    hover->SetDisplayMode(AIS_Shaded);
    hover->SetZLayer(object->ZLayer());
    object->SetDynamicHilightAttributes(hover);
}

std::optional<gp_Ax3> sketchPlaneOf(const Pick& pick)
{
    switch (pick.kind) {
    case Pick::Kind::OriginPlane:
        return core::originPlane(pick.plane);
    case Pick::Kind::ConstructionPlane:
        return pick.construction.frame;
    case Pick::Kind::Face: {
        if (pick.shape.IsNull() || pick.shape.ShapeType() != TopAbs_FACE) {
            return std::nullopt;
        }
        return geometry::planarFrame(TopoDS::Face(pick.shape));
    }
    case Pick::Kind::Edge:
    case Pick::Kind::Vertex:
    case Pick::Kind::Profile:
    case Pick::Kind::SketchLine:
    case Pick::Kind::SketchPoint:
    case Pick::Kind::OriginAxis:
    case Pick::Kind::ConstructionAxis:
    case Pick::Kind::ConstructionPoint:
        return std::nullopt;
    }
    return std::nullopt;
}

Scene::Scene(viewer::OcctViewWidget* view, core::SnapshotCache& snapshots, pick::SceneRays& rays)
    : m_view(view), m_snapshots(&snapshots), m_rays(&rays)
{
    const Handle(Prs3d_Drawer)& drawer = m_view->aisContext()->DefaultDrawer();
    drawer->SetTypeOfDeflection(Aspect_TOD_RELATIVE);
    drawer->SetDeviationCoefficient(geometry::kDisplayDeviationCoefficient);
    drawer->SetDeviationAngle(geometry::kDisplayDeviationAngle);
    updateBoxSelection();
    connect(m_view, &viewer::OcctViewWidget::selectionChanged, this, &PickSource::selectionChanged);
    connect(m_view, &viewer::OcctViewWidget::hoverChanged, this, &PickSource::hoverChanged);
}

Scene::~Scene()
{
    m_view->setBoxSelection({});
}

void Scene::updateBoxSelection()
{
    if (m_mode != PickMode::None && m_mode != PickMode::Edges && m_mode != PickMode::Faces) {
        m_view->setBoxSelection({});
        return;
    }
    m_view->setBoxSelection([this](const std::vector<Handle(SelectMgr_EntityOwner)>& owners) {
        pick::Meshes all;
        pick::Meshes shown;
        for (const core::Body& body : m_model.bodies) {
            all.push_back(m_snapshots->body(body)->mesh);
            if (!m_hiddenBodies.contains(body.key())) {
                shown.push_back(all.back());
            }
        }
        pick::VisibleRays rays = m_rays->rays(all, shown);
        std::vector<const snapshot::Mesh*> meshes;
        meshes.reserve(rays.meshes.size());
        for (const auto& mesh : rays.meshes) {
            meshes.push_back(mesh.get());
        }
        const geometry::ViewBlockers blockers(rays.scene, meshes, std::move(rays.shown));
        std::vector<TopoDS_Shape> shapes(owners.size());
        for (std::size_t i = 0; i < owners.size(); ++i) {
            const std::optional<Pick> pick = pickOf(owners[i]);
            if (pick && (pick->kind == Pick::Kind::Face || pick->kind == Pick::Kind::Edge ||
                         pick->kind == Pick::Kind::Vertex)) {
                shapes[i] = pick->shape;
            }
        }
        std::vector<char> visible(shapes.size(), 0);
        OSD_Parallel::For(0, static_cast<int>(shapes.size()), [&](int i) {
            const TopoDS_Shape& shape = shapes[static_cast<std::size_t>(i)];
            if (shape.IsNull()) {
                return;
            }
            try {
                visible[static_cast<std::size_t>(i)] = isVisible(shape, blockers) ? 1 : 0;
            } catch (const Standard_Failure&) {
                visible[static_cast<std::size_t>(i)] = 1;
            }
        });
        return std::vector<bool>(visible.begin(), visible.end());
    });
}

bool Scene::isVisible(const TopoDS_Shape& shape, const geometry::ViewBlockers& blockers) const
{
    return std::ranges::any_of(geometry::samplePoints(shape), [&](const gp_Pnt& point) {
        return !blockers.blocks(point, m_view->towardViewer(point));
    });
}

void Scene::show(const core::Document& document, const core::Model& model, const DesignSession& session)
{
    m_document = document;
    m_model = model;
    m_hidden.clear();
    for (const core::Feature& feature : document.features()) {
        if (!session.isVisible(feature.id)) {
            m_hidden.insert(feature.id);
        }
    }
    m_hiddenBodies.clear();
    for (const core::Body& body : model.bodies) {
        if (!session.isBodyVisible(body.key())) {
            m_hiddenBodies.insert(body.key());
        }
    }
    m_originVisible = session.isOriginVisible();
    rebuild();
}

void Scene::setDrawsBodies(bool draws)
{
    if (m_drawsBodies == draws) {
        return;
    }
    m_drawsBodies = draws;
    dropBodies();
}

void Scene::setPickMode(PickMode mode)
{
    if (m_mode == mode) {
        return;
    }
    K137_DEBUG(Viewer, "Pick mode {} to {}", toString(m_mode), toString(mode));
    m_preselection = m_mode == PickMode::None ? m_source->selected() : std::vector<Pick>{};
    m_view->aisContext()->ClearSelected(false);
    m_mode = mode;
    updateBoxSelection();
    dropBodies();
    rebuild();
    emit pickModeChanged();
}

void Scene::setEditingSketch(core::FeatureId sketch)
{
    if (m_editingSketch == sketch) {
        return;
    }
    m_editingSketch = sketch;
    rebuild();
    emit pickModeChanged();
}

void Scene::setVisualStyle(VisualStyle style)
{
    if (m_style == style) {
        return;
    }
    m_style = style;
    dropBodies();
    rebuild();
}

void Scene::restyle()
{
    dropBodies();
    rebuild();
}

void Scene::setHighlightedProfiles(std::set<std::pair<core::FeatureId, std::size_t>> profiles)
{
    m_highlightedProfiles = std::move(profiles);
}

void Scene::setHighlightedEdges(std::vector<TopoDS_Edge> edges)
{
    m_highlightedEdges = std::move(edges);
}

void Scene::setHighlightedFaces(std::vector<TopoDS_Face> faces)
{
    m_highlightedFaces = std::move(faces);
}

void Scene::setHighlightedBodies(std::vector<TopoDS_Shape> shapes)
{
    m_highlightedBodies = std::move(shapes);
}

void Scene::setPreviewBodies(std::set<core::BodyKey> keys)
{
    m_previewBodies = std::move(keys);
}

std::optional<Pick> Scene::pickOf(const Handle(SelectMgr_EntityOwner)& owner) const
{
    if (owner.IsNull()) {
        return std::nullopt;
    }
    const Handle(AIS_InteractiveObject) object = Handle(AIS_InteractiveObject)::DownCast(owner->Selectable());
    auto it = m_picks.find(object.get());
    if (it == m_picks.end()) {
        return std::nullopt;
    }
    Pick pick = it->second;
    if (Handle(StdSelect_BRepOwner) shapeOwner = Handle(StdSelect_BRepOwner)::DownCast(owner);
        !shapeOwner.IsNull() && shapeOwner->HasShape()) {
        pick.shape = shapeOwner->Shape();
    }
    if (pick.kind == Pick::Kind::Face && !pick.shape.IsNull()) {
        if (pick.shape.ShapeType() == TopAbs_EDGE) {
            pick.kind = Pick::Kind::Edge;
        } else if (pick.shape.ShapeType() == TopAbs_VERTEX) {
            pick.kind = Pick::Kind::Vertex;
        }
    }
    return pick;
}

std::optional<Pick> Scene::pickWithPoint(const Handle(SelectMgr_EntityOwner)& owner, const PickedPoints& points) const
{
    std::optional<Pick> pick = pickOf(owner);
    if (pick && !pick->point) {
        if (auto it = points.find(owner.get()); it != points.end()) {
            pick->point = it->second;
        }
    }
    return pick;
}

Scene::PickedPoints Scene::pickedPoints() const
{
    PickedPoints points;
    const Handle(StdSelect_ViewerSelector3d)& selector = m_view->aisContext()->MainSelector();
    for (int i = 1; i <= selector->NbPicked(); ++i) {
        points.try_emplace(selector->Picked(i).get(), selector->PickedPoint(i));
    }
    return points;
}

std::vector<Pick> Scene::takePicks()
{
    std::vector<Pick> picks = selected();
    K137_DEBUG(Input, "Took {} picks in {} mode", picks.size(), toString(m_mode));
    m_view->aisContext()->ClearSelected(false);
    m_view->redraw();
    return picks;
}

std::vector<Pick> Scene::selected() const
{
    std::vector<Pick> picks;
    const Handle(AIS_InteractiveContext)& context = m_view->aisContext();
    const PickedPoints points = pickedPoints();
    for (context->InitSelected(); context->MoreSelected(); context->NextSelected()) {
        if (std::optional<Pick> pick = pickWithPoint(context->SelectedOwner(), points)) {
            picks.push_back(std::move(*pick));
        }
    }
    return picks;
}

bool Scene::selectHovered()
{
    const Handle(AIS_InteractiveContext)& context = m_view->aisContext();
    if (!context->HasDetected() || !pickOf(context->DetectedOwner())) {
        return false;
    }
    context->SelectDetected(AIS_SelectionScheme_Replace);
    m_view->redraw();
    return true;
}

void Scene::clearSelection()
{
    m_view->aisContext()->ClearSelected(false);
    m_view->redraw();
}

bool Scene::selectedByBox() const
{
    return m_view->selectedByBox();
}

std::optional<Pick> Scene::hovered() const
{
    const Handle(AIS_InteractiveContext)& context = m_view->aisContext();
    if (!context->HasDetected()) {
        return std::nullopt;
    }
    return pickWithPoint(context->DetectedOwner(), pickedPoints());
}

void Scene::display(const Handle(AIS_InteractiveObject)& object, int selectionMode, std::optional<Pick> pick,
                    int displayMode)
{
    m_view->aisContext()->Display(object, displayMode, selectionMode, false);
    m_objects.push_back(object);
    if (pick) {
        m_picks.emplace(object.get(), std::move(*pick));
    }
}

void Scene::dropBodies()
{
    for (const BodyObject& body : m_bodies) {
        m_view->aisContext()->Remove(body.object, false);
    }
    m_bodies.clear();
    dropErased();
}

void Scene::dropErased()
{
    for (const BodyObject& body : m_erased) {
        m_view->aisContext()->Remove(body.object, false);
    }
    m_erased.clear();
}

void Scene::displayBody(const BodyObject& body)
{
    const Handle(AIS_InteractiveContext)& context = m_view->aisContext();
    context->Display(body.object, body.displayMode,
                     m_bodySelection.empty() ? kNotSelectable : AIS_Shape::SelectionMode(m_bodySelection.front()),
                     false);
    for (std::size_t i = 1; i < m_bodySelection.size(); ++i) {
        context->Activate(body.object, AIS_Shape::SelectionMode(m_bodySelection[i]));
    }
}

bool Scene::setBodiesVisible(const std::vector<core::BodyKey>& keys, bool visible)
{
    if (!m_drawsBodies) {
        for (const core::BodyKey& key : keys) {
            if (visible) {
                m_hiddenBodies.erase(key);
            } else {
                m_hiddenBodies.insert(key);
            }
        }
        return true;
    }
    const auto byKey = [](const core::BodyKey& key) {
        return [&key](const BodyObject& body) { return body.key == key; };
    };
    if (visible && std::ranges::any_of(keys, [&](const core::BodyKey& key) {
            return m_hiddenBodies.contains(key) && std::ranges::none_of(m_erased, byKey(key));
        })) {
        return false;
    }
    for (const core::BodyKey& key : keys) {
        if (visible) {
            if (m_hiddenBodies.erase(key) == 0) {
                continue;
            }
            const auto erased = std::ranges::find_if(m_erased, byKey(key));
            displayBody(*erased);
            m_bodies.push_back(std::move(*erased));
            m_erased.erase(erased);
        } else if (m_hiddenBodies.insert(key).second) {
            const auto shown = std::ranges::find_if(m_bodies, byKey(key));
            if (shown == m_bodies.end()) {
                continue;
            }
            m_view->aisContext()->Erase(shown->object, false);
            m_erased.push_back(std::move(*shown));
            m_bodies.erase(shown);
        }
    }
    m_view->redraw();
    return true;
}

void Scene::rebuild()
{
    [[maybe_unused]] const auto start = std::chrono::steady_clock::now();
    const Handle(AIS_InteractiveContext)& context = m_view->aisContext();
    for (const Handle(AIS_InteractiveObject)& object : m_objects) {
        context->Remove(object, false);
    }
    m_objects.clear();
    m_picks.clear();

    showOrigin();
    showConstructionPlanes();
    showConstructionAxes();
    showConstructionPoints();
    [[maybe_unused]] const std::size_t reused = showBodies();
    showSketches();
    showHighlights();

    K137_TRACE(Viewer, "Scene rebuilt in {:.1f} ms with {} objects, {} bodies of which {} reused, mode {}",
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
               m_objects.size(), m_bodies.size(), reused, toString(m_mode));
    m_view->redraw();
    emit rebuilt();
}

void Scene::showSketches()
{
    const Handle(AIS_InteractiveContext)& context = m_view->aisContext();
    const bool showProfiles =
        m_mode == PickMode::Profiles || m_mode == PickMode::ProfilesAndLines || m_mode == PickMode::PressPull;
    const bool linesPickable = m_mode == PickMode::ProfilesAndLines || m_mode == PickMode::Lines;
    for (const core::Feature& feature : m_document.features()) {
        const auto* data = std::get_if<core::SketchFeature>(&feature.data);
        auto state = m_model.sketches.find(feature.id);
        if (data == nullptr || state == m_model.sketches.end() || feature.id == m_editingSketch ||
            m_hidden.contains(feature.id)) {
            continue;
        }
        for (const auto& [curveId, curve] : data->sketch.curves()) {
            const std::optional<TopoDS_Edge> edge = sketch::makeEdge(data->sketch, curveId, state->second.plane);
            if (!edge) {
                continue;
            }
            Handle(AIS_Shape) object =
                makeWire(*edge,
                         curve.construction ? theme::palette().viewport.sketchConstruction
                                            : theme::palette().viewport.sketchFree,
                         curve.construction ? 1.0 : 1.5, curve.construction ? Aspect_TOL_DASH : Aspect_TOL_SOLID);
            object->SetZLayer(m_view->linesLayer());
            const bool pickable = linesPickable && curve.kind == sketch::CurveKind::Line;
            Pick pick;
            pick.kind = Pick::Kind::SketchLine;
            pick.feature = feature.id;
            pick.index = curveId;
            display(object, pickable ? kWholeObject : kNotSelectable, pick);
        }
        if (m_mode == PickMode::HolePlacement) {
            for (const auto& [pointId, point] : state->second.solved.points()) {
                const gp_Pnt world = sketch::toWorld(state->second.plane, point.pos);
                Handle(AIS_Point) marker = new AIS_Point(new Geom_CartesianPoint(world));
                marker->SetMarker(Aspect_TOM_O_POINT);
                marker->SetColor(occt(theme::palette().viewport.sketchFree));
                marker->Attributes()->PointAspect()->SetScale(kSketchPointScale);
                marker->SetZLayer(Graphic3d_ZLayerId_Topmost);
                Pick pick;
                pick.kind = Pick::Kind::SketchPoint;
                pick.feature = feature.id;
                pick.index = pointId;
                pick.point = world;
                display(marker, kWholeObject, pick, AIS_WireFrame);
            }
        }
        if (!showProfiles) {
            continue;
        }
        const std::vector<TopoDS_Face>& profiles = state->second.profiles;
        for (std::size_t i = 0; i < profiles.size(); ++i) {
            const bool highlighted = m_highlightedProfiles.contains({feature.id, i});
            Handle(AIS_Shape) object =
                makeSurface(profiles[i], highlighted ? theme::palette().chrome.accent : theme::palette().viewport.plane,
                            highlighted ? kHighlightTransparency : kProfileTransparency);
            if (highlighted) {
                object->Attributes()->ShadingAspect()->Aspect()->SetPolygonOffsets(Aspect_POM_Fill, -1.0f, -1.0f);
            } else {
                object->SetZLayer(Graphic3d_ZLayerId_Topmost);
            }
            hoverInOwnLayer(object, context);
            Pick pick;
            pick.kind = Pick::Kind::Profile;
            pick.feature = feature.id;
            pick.index = static_cast<int>(i);
            display(object, kWholeObject, pick);
        }
    }
}

std::size_t Scene::showBodies()
{
    const Handle(AIS_InteractiveContext)& context = m_view->aisContext();
    const std::vector<TopAbs_ShapeEnum> selection = bodySelection(m_mode, m_editingSketch != 0);
    if (selection != m_bodySelection) {
        dropBodies();
        m_bodySelection = selection;
    }
    dropErased();
    if (!m_drawsBodies) {
        return 0;
    }
    std::vector<BodyObject> previous = std::exchange(m_bodies, {});
    std::size_t reused = 0;
    for (const core::Body& body : m_model.bodies) {
        if (m_hiddenBodies.contains(body.key())) {
            continue;
        }
        Pick pick;
        pick.kind = m_mode == PickMode::Edges || m_mode == PickMode::Lines ? Pick::Kind::Edge : Pick::Kind::Face;
        pick.feature = body.id;
        pick.index = body.part;
        const bool preview = m_previewBodies.contains(body.key());
        auto cached = std::ranges::find_if(previous, [&](const BodyObject& b) {
            return b.shape.IsEqual(body.shape) && b.color == body.color && b.faceColors == body.faceColors &&
                   b.preview == preview;
        });
        if (cached != previous.end()) {
            cached->key = body.key();
            m_picks.emplace(cached->object.get(), pick);
            m_bodies.push_back(std::move(*cached));
            previous.erase(cached);
            ++reused;
            continue;
        }
        const auto [object, displayMode] = makeBody(body, m_style);
        if (m_mode == PickMode::Planes) {
            object->SetTransparency(kPlanePickBodyTransparency);
        } else if (preview) {
            object->SetTransparency(kPreviewBodyTransparency);
        }
        m_picks.emplace(object.get(), pick);
        m_bodies.push_back({body.key(), body.shape, Handle(AIS_InteractiveObject)(object), body.color, body.faceColors,
                            preview, displayMode});
        displayBody(m_bodies.back());
    }
    for (const BodyObject& stale : previous) {
        context->Remove(stale.object, false);
    }
    return reused;
}

void Scene::showHighlights()
{
    for (const TopoDS_Edge& edge : m_highlightedEdges) {
        Handle(AIS_Shape) object = makeWire(edge, theme::palette().chrome.accent, kHighlightEdgeWidth);
        object->SetZLayer(Graphic3d_ZLayerId_Topmost);
        Pick pick;
        pick.kind = Pick::Kind::Edge;
        display(object, kWholeObject, pick);
    }
    for (const TopoDS_Face& face : m_highlightedFaces) {
        Handle(AIS_Shape) object = makeSurface(face, theme::palette().chrome.accent, kHighlightTransparency);
        object->Attributes()->ShadingAspect()->Aspect()->SetPolygonOffsets(Aspect_POM_Fill, -1.0f, -1.0f);
        Pick pick;
        pick.kind = Pick::Kind::Face;
        display(object, kWholeObject, pick);
    }
    if (!m_drawsBodies) {
        return;
    }
    for (const TopoDS_Shape& body : m_highlightedBodies) {
        Handle(AIS_Shape) object = makeSurface(body, theme::palette().chrome.accent, kHighlightTransparency);
        object->Attributes()->ShadingAspect()->Aspect()->SetPolygonOffsets(Aspect_POM_Fill, -1.0f, -1.0f);
        display(object, kNotSelectable, std::nullopt);
    }
}

} // namespace kalve137::app
