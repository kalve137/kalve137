// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/viewport_picker.h"

#include "app/session/design_session.h"
#include "app/theme/theme.h"
#include "app/view/band_overlay.h"
#include "app/view/scene.h"
#include "camera/camera.h"
#include "core/recompute.h"
#include "geometry/topology.h"
#include "log/log.h"
#include "pick/scene_rays.h"
#include "render/box_select.h"
#include "render/rhi/viewport.h"
#include "snapshot/body.h"
#include "viewer/box_band.h"

#include <QColor>
#include <QMetaObject>
#include <QRect>
#include <QRectF>

#include <OSD_Parallel.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <gp.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace kalve137::app {

namespace {

constexpr double kClickSlop = 4.0;

bool contains(const std::vector<TopAbs_ShapeEnum>& types, TopAbs_ShapeEnum type)
{
    return std::ranges::find(types, type) != types.end();
}

template <typename Map>
const typename Map::mapped_type* findIn(const Map& shown, const Map& saved, core::FeatureId id)
{
    if (const auto at = shown.find(id); at != shown.end()) {
        return &at->second;
    }
    const auto at = saved.find(id);
    return at != saved.end() ? &at->second : nullptr;
}

snapshot::Color colorOf(const QColor& color)
{
    return {static_cast<float>(color.redF()), static_cast<float>(color.greenF()), static_cast<float>(color.blueF()),
            static_cast<float>(color.alphaF())};
}

} // namespace

ViewportPicker::ViewportPicker(render::Viewport& view, const DesignSession& session, Scene& scene,
                               pick::SceneRays& rays, QObject* parent)
    : PickSource(parent), m_view(&view), m_session(&session), m_scene(&scene), m_rays(&rays),
      m_band(new BandOverlay(&view))
{
    connect(m_view, &render::Viewport::mousePressed, this, &ViewportPicker::press);
    connect(m_view, &render::Viewport::mouseReleased, this,
            [this](QPointF position, Qt::MouseButton button, Qt::MouseButtons, Qt::KeyboardModifiers modifiers) {
                release(position, button, modifiers);
            });
    connect(m_view, &render::Viewport::mouseMoved, this,
            [this](QPointF position, Qt::MouseButtons buttons, Qt::KeyboardModifiers) { move(position, buttons); });
    connect(m_view, &render::Viewport::mouseLeft, this, [this] { setHovered(std::nullopt); });
    connect(m_view, &render::Viewport::sceneChanged, this, &ViewportPicker::dropStale);
    connect(m_scene, &Scene::pickModeChanged, this, [this] {
        m_selected.clear();
        m_hovered.reset();
        m_pressed.reset();
        m_boxing = false;
        m_band->hideBand();
        showHighlight();
    });
}

bool ViewportPicker::canBox() const
{
    const PickMode mode = m_scene->pickMode();
    return mode == PickMode::None || mode == PickMode::Edges || mode == PickMode::Faces;
}

std::shared_ptr<const snapshot::Mesh> ViewportPicker::meshOf(const render::PickedItem& item) const
{
    for (const auto& body : m_view->pickScene().bodies) {
        if (body->feature == item.feature && body->part == item.part) {
            return body->mesh;
        }
    }
    return nullptr;
}

std::vector<render::PickedItem> ViewportPicker::boxItems(QPointF from, QPointF to) const
{
    const camera::ScreenPoint start{from.x(), from.y()};
    const camera::ScreenPoint end{to.x(), to.y()};
    const snapshot::Scene& scene = m_view->pickScene();
    const camera::Viewport viewport = m_view->viewport();
    std::vector<render::PickedItem> candidates =
        render::boxCandidates(scene.bodies, m_view->camera(), viewport, render::ScreenRect::spanning(start, end),
                              render::isCrossingBox(start, end), kinds());
    if (candidates.empty()) {
        return candidates;
    }
    std::vector<TopoDS_Shape> shapes(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (const std::optional<Pick> pick = pickOf(
                {.item = candidates[i], .mesh = nullptr, .point = std::nullopt, .datum = std::nullopt, .sketch = {}})) {
            shapes[i] = pick->shape;
        }
    }
    const pick::VisibleRays rays = m_rays->rays(scene);
    std::vector<const snapshot::Mesh*> meshes;
    meshes.reserve(rays.meshes.size());
    for (const auto& mesh : rays.meshes) {
        meshes.push_back(mesh.get());
    }
    const geometry::ViewBlockers blockers(rays.scene, meshes, rays.shown);
    camera::Camera shown = m_view->camera();
    shown.setAspect(viewport.aspect());
    const auto towardViewer = [&](const gp_Pnt& point) {
        const camera::Vec3 back = shown.direction() * -1.0;
        if (shown.isOrthographic()) {
            return gp_Dir(back.x, back.y, back.z);
        }
        const camera::Vec3 toEye = shown.eye() - camera::Vec3{point.X(), point.Y(), point.Z()};
        const double length = camera::length(toEye);
        return length > Precision::Confusion() ? gp_Dir(toEye.x / length, toEye.y / length, toEye.z / length)
                                               : gp_Dir(back.x, back.y, back.z);
    };
    std::vector<char> visible(shapes.size(), 0);
    OSD_Parallel::For(0, static_cast<int>(shapes.size()), [&](int i) {
        const TopoDS_Shape& shape = shapes[static_cast<std::size_t>(i)];
        if (shape.IsNull()) {
            return;
        }
        try {
            visible[static_cast<std::size_t>(i)] =
                std::ranges::any_of(geometry::samplePoints(shape),
                                    [&](const gp_Pnt& point) { return !blockers.blocks(point, towardViewer(point)); })
                    ? 1
                    : 0;
        } catch (const Standard_Failure&) {
            visible[static_cast<std::size_t>(i)] = 1;
        }
    });
    std::vector<render::PickedItem> items;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (visible[i] != 0) {
            items.push_back(candidates[i]);
        }
    }
    std::ranges::sort(items);
    return items;
}

std::optional<std::vector<render::PickedItem>> ViewportPicker::boxIdItems(QPointF from, QPointF to) const
{
    const double ratio = m_view->devicePixelRatioF();
    const QRectF band = QRectF(from * ratio, to * ratio).normalized();
    const QRect pixels(QPoint(static_cast<int>(std::ceil(band.left())), static_cast<int>(std::ceil(band.top()))),
                       QPoint(static_cast<int>(std::floor(band.right())), static_cast<int>(std::floor(band.bottom()))));
    const render::PickKinds wanted = kinds();
    return m_view->itemsIn(pixels, !render::isCrossingBox({from.x(), from.y()}, {to.x(), to.y()}), wanted,
                           {.points = wanted.points});
}

void ViewportPicker::boxSelect(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers)
{
    const bool additive = (modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) != 0;
    if (!additive) {
        m_selected.clear();
    }
    for (const render::PickedItem& item : boxItems(from, to)) {
        if (std::ranges::none_of(m_selected,
                                 [&](const Found& found) { return !found.overlay() && found.item == item; })) {
            m_selected.push_back(
                {.item = item, .mesh = meshOf(item), .point = std::nullopt, .datum = std::nullopt, .sketch = {}});
        }
    }
    K137_DEBUG(Input, "{} box selected {}",
               render::isCrossingBox({from.x(), from.y()}, {to.x(), to.y()}) ? "Crossing" : "Window",
               m_selected.size());
    m_selectedByBox = true;
    showHighlight();
    announce(&PickSource::selectionChanged);
}

render::PickKinds ViewportPicker::kinds() const
{
    const PickMode mode = m_scene->pickMode();
    const std::vector<TopAbs_ShapeEnum> types = bodySelection(mode, m_scene->editingSketch() != 0);
    return {.faces = contains(types, TopAbs_FACE),
            .edges = contains(types, TopAbs_EDGE),
            .points = contains(types, TopAbs_VERTEX),
            .planes = planesPickable(mode),
            .axes = axesPickable(mode),
            .markers = mode == PickMode::AxisSources,
            .sketchLines = mode == PickMode::Lines || mode == PickMode::ProfilesAndLines,
            .sketchPoints = mode == PickMode::HolePlacement,
            .profiles =
                mode == PickMode::Profiles || mode == PickMode::ProfilesAndLines || mode == PickMode::PressPull};
}

std::optional<ViewportPicker::Found> ViewportPicker::pickAt(QPointF position) const
{
    const render::PickKinds wanted = kinds();
    if (!wanted.any()) {
        return std::nullopt;
    }
    const std::optional<render::IdPick> hit =
        m_view->pickAt(position, wanted,
                       {.points = wanted.points,
                        .planes = wanted.planes,
                        .axes = wanted.axes,
                        .datumsThroughBodies = !wanted.faces && !wanted.edges && !wanted.points,
                        .markers = wanted.markers,
                        .sketchLines = wanted.sketchLines,
                        .sketchPoints = wanted.sketchPoints,
                        .profiles = wanted.profiles});
    if (!hit) {
        return std::nullopt;
    }
    Found found{.item = hit->item, .mesh = nullptr, .point = std::nullopt, .datum = hit->datum, .sketch = hit->sketch};
    if (!found.overlay()) {
        found.mesh = meshOf(hit->item);
    }
    const render::DepthReach reach = found.overlay() ? render::DepthReach::PastFar : render::DepthReach::InsideFar;
    if (const std::optional<camera::Vec3> at = m_view->pointOf(hit->hit.x, hit->hit.y, hit->hit.depth, reach)) {
        found.point = gp_Pnt(at->x, at->y, at->z);
    }
    if (found.sketch) {
        if (const std::optional<snapshot::Point> exact = render::sketchPointOf(m_view->sketches(), *found.sketch)) {
            found.point = gp_Pnt((*exact)[0], (*exact)[1], (*exact)[2]);
        }
    }
    return found;
}

const core::Body* ViewportPicker::bodyOf(const render::PickedItem& item) const
{
    return bodyOf(core::BodyKey{.feature = item.feature, .part = item.part});
}

const core::Body* ViewportPicker::bodyOf(core::BodyKey key) const
{
    const core::Body* body = m_shown.body(key);
    return body != nullptr ? body : m_session->model().body(key);
}

void ViewportPicker::addOutline(const render::PickedItem& face, std::set<render::PickedItem>& outline) const
{
    const core::Body* body = bodyOf(face);
    if (face.kind != render::PickKind::Face || body == nullptr || body->shape.IsNull()) {
        return;
    }
    app::addOutline(*body, face.index, m_shapes, outline);
}

std::optional<Pick> ViewportPicker::datumPick(const render::DatumKey& key, const std::optional<gp_Pnt>& point) const
{
    Pick pick;
    pick.point = point;
    const auto id = static_cast<core::FeatureId>(key.id);
    switch (key.kind) {
    case render::DatumKind::OriginPlane:
        pick.kind = Pick::Kind::OriginPlane;
        pick.plane = static_cast<core::OriginPlane>(key.id);
        return pick;
    case render::DatumKind::OriginAxis: {
        pick.kind = Pick::Kind::OriginAxis;
        pick.axis = static_cast<core::AxisKind>(key.id);
        const gp_Dir direction = pick.axis == core::AxisKind::X   ? gp::DX()
                                 : pick.axis == core::AxisKind::Y ? gp::DY()
                                                                  : gp::DZ();
        pick.line = gp_Ax1(gp::Origin(), direction);
        return pick;
    }
    case render::DatumKind::Plane: {
        const core::ConstructionPlane* plane = findIn(m_shown.planes, m_session->model().planes, id);
        if (plane == nullptr) {
            return std::nullopt;
        }
        pick.kind = Pick::Kind::ConstructionPlane;
        pick.feature = id;
        pick.construction = *plane;
        return pick;
    }
    case render::DatumKind::Axis: {
        const gp_Ax1* axis = findIn(m_shown.axes, m_session->model().axes, id);
        if (axis == nullptr) {
            return std::nullopt;
        }
        pick.kind = Pick::Kind::ConstructionAxis;
        pick.feature = id;
        pick.line = *axis;
        return pick;
    }
    case render::DatumKind::Point: {
        const gp_Pnt* location = findIn(m_shown.points, m_session->model().points, id);
        if (location == nullptr) {
            return std::nullopt;
        }
        pick.kind = Pick::Kind::ConstructionPoint;
        pick.feature = id;
        pick.point = *location;
        return pick;
    }
    }
    return std::nullopt;
}

Pick ViewportPicker::sketchPick(const render::SketchKey& key, const std::optional<gp_Pnt>& point)
{
    Pick pick;
    switch (key.item) {
    case render::SketchItem::Line:
        pick.kind = Pick::Kind::SketchLine;
        break;
    case render::SketchItem::Point:
        pick.kind = Pick::Kind::SketchPoint;
        break;
    case render::SketchItem::Profile:
        pick.kind = Pick::Kind::Profile;
        break;
    }
    pick.feature = static_cast<core::FeatureId>(key.feature);
    pick.index = key.index;
    pick.point = point;
    return pick;
}

std::optional<Pick> ViewportPicker::pickOf(const Found& found) const
{
    if (found.datum) {
        return datumPick(*found.datum, found.point);
    }
    if (found.sketch) {
        return sketchPick(*found.sketch, found.point);
    }
    const core::Body* body = bodyOf(found.item);
    if (body == nullptr || body->shape.IsNull()) {
        return std::nullopt;
    }
    const BodyShapes& items = m_shapes.of(*body);
    const ShapeIndex* map = nullptr;
    Pick pick;
    switch (found.item.kind) {
    case render::PickKind::None:
        return std::nullopt;
    case render::PickKind::Face:
        map = &items.faces;
        pick.kind = Pick::Kind::Face;
        break;
    case render::PickKind::Edge:
        map = &items.edges;
        pick.kind = Pick::Kind::Edge;
        break;
    case render::PickKind::Point:
        map = &items.vertices;
        pick.kind = Pick::Kind::Vertex;
        break;
    }
    const auto index = static_cast<int>(found.item.index) + 1;
    if (index > map->Extent()) {
        return std::nullopt;
    }
    pick.feature = body->id;
    pick.index = body->part;
    pick.shape = map->FindKey(index);
    pick.point = found.point;
    return pick;
}

std::optional<Pick> ViewportPicker::hovered() const
{
    return m_hovered ? pickOf(*m_hovered) : std::nullopt;
}

std::vector<Pick> ViewportPicker::selected() const
{
    std::vector<Pick> picks;
    picks.reserve(m_selected.size());
    for (const Found& found : m_selected) {
        if (std::optional<Pick> pick = pickOf(found)) {
            picks.push_back(std::move(*pick));
        }
    }
    return picks;
}

std::vector<Pick> ViewportPicker::takePicks()
{
    std::vector<Pick> picks = selected();
    m_selected.clear();
    showHighlight();
    return picks;
}

bool ViewportPicker::selectHovered()
{
    if (!m_hovered || !pickOf(*m_hovered)) {
        return false;
    }
    m_selected = {*m_hovered};
    showHighlight();
    return true;
}

void ViewportPicker::clearSelection()
{
    m_selected.clear();
    showHighlight();
}

void ViewportPicker::hoverAt(QPointF position)
{
    setHovered(pickAt(position));
}

void ViewportPicker::clickAt(QPointF position, Qt::KeyboardModifiers modifiers)
{
    std::optional<Found> found = pickAt(position);
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        if (found) {
            const auto same =
                std::ranges::find_if(m_selected, [&](const Found& selected) { return selected.same(*found); });
            if (same != m_selected.end()) {
                m_selected.erase(same);
            } else {
                m_selected.push_back(std::move(*found));
            }
        }
    } else if (found) {
        m_selected = {std::move(*found)};
    } else {
        m_selected.clear();
    }
    showHighlight();
    announce(&PickSource::selectionChanged);
}

void ViewportPicker::press(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons,
                           Qt::KeyboardModifiers modifiers)
{
    m_boxing = false;
    m_band->hideBand();
    if (button == Qt::LeftButton) {
        m_selectedByBox = false;
    }
    if (button != Qt::LeftButton || buttons != Qt::LeftButton) {
        m_pressed.reset();
        return;
    }
    m_pressed = position;
    m_pressModifiers = modifiers;
    m_boxFromPress = canBox() && !pickAt(position);
    if (m_boxFromPress) {
        m_rays->prepare(m_view->pickScene());
    }
}

void ViewportPicker::release(QPointF position, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    const std::optional<QPointF> pressed = std::exchange(m_pressed, std::nullopt);
    if (button != Qt::LeftButton || !pressed) {
        return;
    }
    if (std::exchange(m_boxing, false)) {
        m_band->hideBand();
        boxSelect(*pressed, position, m_pressModifiers);
        return;
    }
    const QPointF moved = position - *pressed;
    if (std::hypot(moved.x(), moved.y()) <= kClickSlop) {
        clickAt(position, modifiers);
    }
}

void ViewportPicker::move(QPointF position, Qt::MouseButtons buttons)
{
    if (buttons == Qt::NoButton) {
        hoverAt(position);
        return;
    }
    if (!m_pressed || !m_boxFromPress || buttons != Qt::LeftButton) {
        return;
    }
    if (!m_boxing && !viewer::isBoxDrag(*m_pressed, position)) {
        return;
    }
    if (!m_boxing) {
        m_boxing = true;
        setHovered(std::nullopt);
    }
    m_band->setColor(theme::palette().chrome.accent);
    m_band->showBand(*m_pressed, position);
}

void ViewportPicker::setHovered(std::optional<Found> found)
{
    const bool same = found.has_value() == m_hovered.has_value() && (!found || found->same(*m_hovered));
    m_hovered = std::move(found);
    if (same) {
        return;
    }
    showHover();
    announce(&PickSource::hoverChanged);
}

void ViewportPicker::dropStale()
{
    const auto stale = [this](const Found& found) {
        if (found.datum) {
            return !render::contains(m_view->datums(), *found.datum);
        }
        if (found.sketch) {
            return std::ranges::find(m_view->sketchGeometry().keys, *found.sketch) ==
                   m_view->sketchGeometry().keys.end();
        }
        return std::ranges::none_of(m_view->pickScene().bodies, [&](const auto& body) {
            return body->feature == found.item.feature && body->part == found.item.part && body->mesh == found.mesh;
        });
    };
    const std::size_t before = m_selected.size();
    std::erase_if(m_selected, stale);
    const bool hoverGone = m_hovered && stale(*m_hovered);
    if (hoverGone) {
        m_hovered.reset();
    }
    const bool remapped = mapPanelPicks();
    if (remapped || hoverGone || m_selected.size() != before) {
        showHighlight();
    }
    if (hoverGone) {
        announce(&PickSource::hoverChanged);
    }
}

void ViewportPicker::showModel(core::Model model)
{
    m_shown = std::move(model);
    if (!m_highlightedBodies.empty()) {
        showHighlight();
    }
}

void ViewportPicker::setHighlightedBodies(std::vector<core::BodyKey> keys)
{
    if (keys == m_highlightedBodies) {
        return;
    }
    m_highlightedBodies = std::move(keys);
    showHover();
}

void ViewportPicker::setPanelPicks(PanelPicks picks)
{
    m_panelPicks = std::move(picks);
    if (mapPanelPicks()) {
        showHighlight();
    }
}

bool ViewportPicker::mapPanelPicks()
{
    std::vector<const core::Body*> shown;
    if (!m_panelPicks.empty()) {
        shown.reserve(m_view->pickScene().bodies.size());
        for (const auto& body : m_view->pickScene().bodies) {
            shown.push_back(bodyOf(core::BodyKey{.feature = body->feature, .part = body->part}));
        }
    }
    PanelItems items = app::panelItems(m_panelPicks, shown, m_shapes);
    if (items == m_panelItems) {
        return false;
    }
    m_panelItems = std::move(items);
    return true;
}

void ViewportPicker::showHighlight()
{
    render::Highlight highlight;
    addHover(highlight);
    for (const Found& found : m_selected) {
        if (found.overlay()) {
            continue;
        }
        highlight.selected.insert(found.item);
        addOutline(found.item, highlight.selectedOutline);
    }
    highlight.selected.insert(m_panelItems.selected.begin(), m_panelItems.selected.end());
    highlight.selectedOutline.insert(m_panelItems.selectedOutline.begin(), m_panelItems.selectedOutline.end());
    m_view->setHighlight(std::move(highlight));
    showMarks();
}

void ViewportPicker::showHover()
{
    render::Highlight highlight;
    addHover(highlight);
    m_view->setHover(std::move(highlight.hovered), std::move(highlight.hoveredOutline), std::move(highlight.tinted));
    showMarks();
}

void ViewportPicker::showMarks()
{
    render::DatumMarks datums;
    render::SketchMarks sketches;
    if (m_hovered) {
        datums.hovered = m_hovered->datum;
        sketches.hovered = m_hovered->sketch;
    }
    for (const Found& found : m_selected) {
        if (found.datum) {
            datums.selected.insert(*found.datum);
        }
        if (found.sketch) {
            sketches.selected.insert(*found.sketch);
        }
    }
    m_view->setDatumMarks(std::move(datums));
    m_view->setSketchMarks(std::move(sketches));
    showVertexMarks();
}

void ViewportPicker::showVertexMarks()
{
    const theme::Palette& palette = theme::palette();
    const VertexColors colors{.hovered = colorOf(palette.viewport.hover), .selected = colorOf(palette.chrome.accent)};
    std::vector<render::MarkerItem> marks;
    const auto add = [&](const render::PickedItem& item, bool selected) {
        if (item.kind != render::PickKind::Point) {
            return;
        }
        if (const core::Body* body = bodyOf(item); body != nullptr && !body->shape.IsNull()) {
            addVertexMark(*body, item.index, selected, colors, m_shapes, marks);
        }
    };
    if (m_hovered && !m_hovered->overlay()) {
        add(m_hovered->item, false);
    }
    for (const Found& found : m_selected) {
        if (!found.overlay()) {
            add(found.item, true);
        }
    }
    for (const render::PickedItem& item : m_panelItems.selected) {
        add(item, true);
    }
    m_view->setVertexMarks(std::move(marks));
}

void ViewportPicker::addHover(render::Highlight& highlight) const
{
    if (m_hovered && !m_hovered->overlay()) {
        highlight.hovered.insert(m_hovered->item);
        addOutline(m_hovered->item, highlight.hoveredOutline);
    }
    for (const core::BodyKey& key : m_highlightedBodies) {
        const core::Body* body = m_shown.body(key);
        if (body == nullptr || body->shape.IsNull()) {
            continue;
        }
        addWholeBody(*body, m_shapes, highlight.hovered, highlight.hoveredOutline);
    }
    highlight.tinted.insert(m_panelItems.tinted.begin(), m_panelItems.tinted.end());
}

void ViewportPicker::announce(void (PickSource::*signal)())
{
    QMetaObject::invokeMethod(this, signal, Qt::QueuedConnection);
}

} // namespace kalve137::app
