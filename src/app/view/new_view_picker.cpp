// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/new_view_picker.h"

#include "app/session/design_session.h"
#include "app/view/scene.h"
#include "camera/camera.h"
#include "core/recompute.h"
#include "render/rhi/rhi_view.h"
#include "snapshot/body.h"

#include <QMetaObject>

#include <TopExp.hxx>

#include <algorithm>
#include <cmath>
#include <utility>

namespace kalve137::app {

namespace {

constexpr double kClickSlop = 4.0;

bool contains(const std::vector<TopAbs_ShapeEnum>& types, TopAbs_ShapeEnum type)
{
    return std::ranges::find(types, type) != types.end();
}

} // namespace

NewViewPicker::NewViewPicker(render::RhiView& view, const DesignSession& session, Scene& scene, QObject* parent)
    : PickSource(parent), m_view(&view), m_session(&session), m_scene(&scene)
{
    connect(m_view, &render::RhiView::mousePressed, this,
            [this](QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers) {
                press(position, button, buttons);
            });
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
        showHighlight();
    });
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
    const camera::Viewport port = m_view->viewport();
    if (hit->hit.depth < 1.0F && port.isValid()) {
        camera::Camera shown = m_view->camera();
        shown.setAspect(port.aspect());
        const camera::Vec3 at = shown.unproject(camera::Vec3{((hit->hit.x + 0.5) / port.width * 2.0) - 1.0,
                                                             1.0 - ((hit->hit.y + 0.5) / port.height * 2.0),
                                                             (static_cast<double>(hit->hit.depth) * 2.0) - 1.0});
        found.point = gp_Pnt(at.x, at.y, at.z);
    }
    return found;
}

const core::Body* NewViewPicker::bodyOf(const render::PickedItem& item) const
{
    const core::BodyKey key{static_cast<core::FeatureId>(item.feature), item.part};
    const core::Body* body = m_scene->model().body(key);
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

void NewViewPicker::press(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    if (button == Qt::LeftButton && buttons == Qt::LeftButton) {
        m_pressed = position;
    } else {
        m_pressed.reset();
    }
}

void NewViewPicker::release(QPointF position, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    const std::optional<QPointF> pressed = std::exchange(m_pressed, std::nullopt);
    if (button != Qt::LeftButton || !pressed) {
        return;
    }
    const QPointF moved = position - *pressed;
    if (std::hypot(moved.x(), moved.y()) <= kClickSlop) {
        clickAt(position, modifiers);
    }
}

void NewViewPicker::move(QPointF position, Qt::MouseButtons buttons)
{
    if (buttons != Qt::NoButton) {
        return;
    }
    hoverAt(position);
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

void NewViewPicker::showHighlight()
{
    render::Highlight highlight;
    if (m_hovered) {
        highlight.hovered.insert(m_hovered->item);
    }
    for (const Found& found : m_selected) {
        highlight.selected.insert(found.item);
    }
    m_view->setHighlight(std::move(highlight));
}

void NewViewPicker::announce(void (PickSource::*signal)())
{
    QMetaObject::invokeMethod(this, signal, Qt::QueuedConnection);
}

} // namespace kalve137::app
