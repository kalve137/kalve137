// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/new_view_picker.h"

#include "app/session/design_session.h"
#include "app/view/band_overlay.h"
#include "app/view/scene.h"
#include "camera/camera.h"
#include "core/recompute.h"
#include "geometry/topology.h"
#include "log/log.h"
#include "pick/scene_rays.h"
#include "render/box_select.h"
#include "render/rhi/rhi_view.h"
#include "snapshot/body.h"
#include "viewer/box_band.h"

#include <QColor>
#include <QMetaObject>
#include <QRect>
#include <QRectF>

#include <OSD_Parallel.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace kalve137::app {

namespace {

constexpr double kClickSlop = 4.0;

bool contains(const std::vector<TopAbs_ShapeEnum>& types, TopAbs_ShapeEnum type)
{
    return std::ranges::find(types, type) != types.end();
}

} // namespace

NewViewPicker::NewViewPicker(render::RhiView& view, const DesignSession& session, Scene& scene, pick::SceneRays& rays,
                             QObject* parent)
    : PickSource(parent), m_view(&view), m_session(&session), m_scene(&scene), m_rays(&rays),
      m_band(new BandOverlay(&view))
{
    connect(m_view, &render::RhiView::mousePressed, this, &NewViewPicker::press);
    connect(m_view, &render::RhiView::mouseReleased, this,
            [this](QPointF position, Qt::MouseButton button, Qt::MouseButtons, Qt::KeyboardModifiers modifiers) {
                release(position, button, modifiers);
            });
    connect(m_view, &render::RhiView::mouseMoved, this,
            [this](QPointF position, Qt::MouseButtons buttons, Qt::KeyboardModifiers) { move(position, buttons); });
    connect(m_view, &render::RhiView::mouseLeft, this, [this] { setHovered(std::nullopt); });
    connect(m_view, &render::RhiView::sceneChanged, this, &NewViewPicker::dropStale);
    connect(m_scene, &Scene::pickModeChanged, this, [this] {
        m_selected.clear();
        m_hovered.reset();
        m_pressed.reset();
        m_boxing = false;
        m_band->hideBand();
        showHighlight();
    });
}

bool NewViewPicker::canBox() const
{
    const PickMode mode = m_scene->pickMode();
    return mode == PickMode::None || mode == PickMode::Edges || mode == PickMode::Faces;
}

std::shared_ptr<const snapshot::Mesh> NewViewPicker::meshOf(const render::PickedItem& item) const
{
    for (const auto& body : m_view->scene().bodies) {
        if (body->feature == item.feature && body->part == item.part) {
            return body->mesh;
        }
    }
    return nullptr;
}

std::vector<render::PickedItem> NewViewPicker::boxItems(QPointF from, QPointF to) const
{
    const camera::ScreenPoint start{from.x(), from.y()};
    const camera::ScreenPoint end{to.x(), to.y()};
    const snapshot::Scene& scene = m_view->scene();
    const camera::Viewport viewport = m_view->viewport();
    std::vector<render::PickedItem> candidates =
        render::boxCandidates(scene.bodies, m_view->camera(), viewport, render::ScreenRect::spanning(start, end),
                              render::isCrossingBox(start, end), kinds());
    if (candidates.empty()) {
        return candidates;
    }
    std::vector<TopoDS_Shape> shapes(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (const std::optional<Pick> pick = pickOf({.item = candidates[i], .mesh = nullptr, .point = std::nullopt})) {
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

std::optional<std::vector<render::PickedItem>> NewViewPicker::boxIdItems(QPointF from, QPointF to) const
{
    const double ratio = m_view->devicePixelRatioF();
    const QRectF band = QRectF(from * ratio, to * ratio).normalized();
    const QRect pixels(QPoint(static_cast<int>(std::ceil(band.left())), static_cast<int>(std::ceil(band.top()))),
                       QPoint(static_cast<int>(std::floor(band.right())), static_cast<int>(std::floor(band.bottom()))));
    const render::PickKinds wanted = kinds();
    return m_view->itemsIn(pixels, !render::isCrossingBox({from.x(), from.y()}, {to.x(), to.y()}), wanted,
                           {.points = wanted.points});
}

void NewViewPicker::boxSelect(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers)
{
    const bool additive = (modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) != 0;
    if (!additive) {
        m_selected.clear();
    }
    for (const render::PickedItem& item : boxItems(from, to)) {
        if (std::ranges::none_of(m_selected, [&](const Found& found) { return found.item == item; })) {
            m_selected.push_back({.item = item, .mesh = meshOf(item), .point = std::nullopt});
        }
    }
    K137_DEBUG(Input, "{} box selected {}",
               render::isCrossingBox({from.x(), from.y()}, {to.x(), to.y()}) ? "Crossing" : "Window",
               m_selected.size());
    m_selectedByBox = true;
    showHighlight();
    announce(&PickSource::selectionChanged);
}

render::PickKinds NewViewPicker::kinds() const
{
    const std::vector<TopAbs_ShapeEnum> types = bodySelection(m_scene->pickMode(), m_scene->editingSketch() != 0);
    return {.faces = contains(types, TopAbs_FACE),
            .edges = contains(types, TopAbs_EDGE),
            .points = contains(types, TopAbs_VERTEX)};
}

std::optional<NewViewPicker::Found> NewViewPicker::pickAt(QPointF position) const
{
    const render::PickKinds wanted = kinds();
    if (!wanted.faces && !wanted.edges && !wanted.points) {
        return std::nullopt;
    }
    const std::optional<render::IdPick> hit = m_view->pickAt(position, wanted, {.points = wanted.points});
    if (!hit) {
        return std::nullopt;
    }
    Found found{.item = hit->item, .mesh = nullptr, .point = std::nullopt};
    for (const auto& body : m_view->scene().bodies) {
        if (body->feature == hit->item.feature && body->part == hit->item.part) {
            found.mesh = body->mesh;
            break;
        }
    }
    if (hit->hit.depth < 1.0F) {
        const camera::Vec3 at = m_view->pointOf(hit->hit.x, hit->hit.y, hit->hit.depth);
        found.point = gp_Pnt(at.x, at.y, at.z);
    }
    return found;
}

const core::Body* NewViewPicker::bodyOf(const render::PickedItem& item) const
{
    const core::BodyKey key{static_cast<core::FeatureId>(item.feature), item.part};
    const core::Body* body = m_shown.body(key);
    return body != nullptr ? body : m_session->model().body(key);
}

std::optional<Pick> NewViewPicker::pickOf(const Found& found) const
{
    const core::Body* body = bodyOf(found.item);
    if (body == nullptr || body->shape.IsNull()) {
        return std::nullopt;
    }
    Items& items = m_items[body->key()];
    if (!items.shape.IsEqual(body->shape)) {
        items = {};
        items.shape = body->shape;
        TopExp::MapShapes(body->shape, TopAbs_FACE, items.faces);
        TopExp::MapShapes(body->shape, TopAbs_EDGE, items.edges);
        TopExp::MapShapes(body->shape, TopAbs_VERTEX, items.vertices);
    }
    const ShapeMap* map = nullptr;
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

std::optional<Pick> NewViewPicker::hovered() const
{
    return m_hovered ? pickOf(*m_hovered) : std::nullopt;
}

std::vector<Pick> NewViewPicker::selected() const
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

std::vector<Pick> NewViewPicker::takePicks()
{
    std::vector<Pick> picks = selected();
    m_selected.clear();
    showHighlight();
    return picks;
}

bool NewViewPicker::selectHovered()
{
    if (!m_hovered || !pickOf(*m_hovered)) {
        return false;
    }
    m_selected = {*m_hovered};
    showHighlight();
    return true;
}

void NewViewPicker::clearSelection()
{
    m_selected.clear();
    showHighlight();
}

void NewViewPicker::hoverAt(QPointF position)
{
    setHovered(pickAt(position));
}

void NewViewPicker::clickAt(QPointF position, Qt::KeyboardModifiers modifiers)
{
    std::optional<Found> found = pickAt(position);
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        if (found) {
            const auto same =
                std::ranges::find_if(m_selected, [&](const Found& selected) { return selected.item == found->item; });
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

void NewViewPicker::press(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons,
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
        m_rays->prepare(m_view->scene());
    }
}

void NewViewPicker::release(QPointF position, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
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

void NewViewPicker::move(QPointF position, Qt::MouseButtons buttons)
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
    const snapshot::Color& color = m_view->viewportStyle().selected;
    m_band->setColor(QColor::fromRgbF(color.red, color.green, color.blue));
    m_band->showBand(*m_pressed, position);
}

void NewViewPicker::setHovered(std::optional<Found> found)
{
    const bool same = found.has_value() == m_hovered.has_value() && (!found || found->item == m_hovered->item);
    m_hovered = std::move(found);
    if (same) {
        return;
    }
    showHighlight();
    announce(&PickSource::hoverChanged);
}

void NewViewPicker::dropStale()
{
    const auto stale = [this](const Found& found) {
        return std::ranges::none_of(m_view->scene().bodies, [&](const auto& body) {
            return body->feature == found.item.feature && body->part == found.item.part && body->mesh == found.mesh;
        });
    };
    const std::size_t before = m_selected.size();
    std::erase_if(m_selected, stale);
    const bool hoverGone = m_hovered && stale(*m_hovered);
    if (hoverGone) {
        m_hovered.reset();
    }
    if (hoverGone || m_selected.size() != before) {
        showHighlight();
    }
    if (hoverGone) {
        announce(&PickSource::hoverChanged);
    }
}

void NewViewPicker::showModel(core::Model model)
{
    m_shown = std::move(model);
    if (!m_highlightedBodies.empty()) {
        showHighlight();
    }
}

void NewViewPicker::setHighlightedBodies(std::vector<core::BodyKey> keys)
{
    if (keys == m_highlightedBodies) {
        return;
    }
    m_highlightedBodies = std::move(keys);
    showHighlight();
}

void NewViewPicker::showHighlight()
{
    render::Highlight highlight;
    if (m_hovered) {
        highlight.hovered.insert(m_hovered->item);
    }
    for (const Found& found : m_selected) {
        highlight.selected.insert(found.item);
    }
    for (const core::BodyKey& key : m_highlightedBodies) {
        const core::Body* body = m_shown.body(key);
        if (body == nullptr || body->shape.IsNull()) {
            continue;
        }
        ShapeMap faces;
        TopExp::MapShapes(body->shape, TopAbs_FACE, faces);
        for (int face = 0; face < faces.Extent(); ++face) {
            highlight.selected.insert({.feature = static_cast<int>(key.feature),
                                       .part = key.part,
                                       .kind = render::PickKind::Face,
                                       .index = static_cast<std::uint32_t>(face)});
        }
    }
    m_view->setHighlight(std::move(highlight));
}

void NewViewPicker::announce(void (PickSource::*signal)())
{
    QMetaObject::invokeMethod(this, signal, Qt::QueuedConnection);
}

} // namespace kalve137::app
