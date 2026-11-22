// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/view/pick_source.h"
#include "core/document.h"
#include "core/recompute.h"
#include "render/chunk_set.h"
#include "render/pick_id.h"

#include <QPointF>

#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Shape.hxx>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace kalve137::snapshot {
struct Mesh;
}

namespace kalve137::render {
class RhiView;
}

namespace kalve137::pick {
class SceneRays;
}

namespace kalve137::app {

class BandOverlay;
class DesignSession;
class Scene;

class NewViewPicker : public PickSource {
    Q_OBJECT

public:
    NewViewPicker(render::RhiView& view, const DesignSession& session, Scene& scene, pick::SceneRays& rays,
                  QObject* parent = nullptr);

    std::optional<Pick> hovered() const override;
    std::vector<Pick> selected() const override;
    std::vector<Pick> takePicks() override;
    bool selectHovered() override;
    void clearSelection() override;
    bool selectedByBox() const override { return m_selectedByBox; }
    void showModel(core::Model model);
    void setHighlightedBodies(std::vector<core::BodyKey> keys);

    void hoverAt(QPointF position);
    void clickAt(QPointF position, Qt::KeyboardModifiers modifiers);
    render::PickKinds kinds() const;
    std::vector<render::PickedItem> boxItems(QPointF from, QPointF to) const;
    std::optional<std::vector<render::PickedItem>> boxIdItems(QPointF from, QPointF to) const;
    void boxSelect(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers);
    bool canBox() const;

private:
    struct Found {
        render::PickedItem item;
        std::shared_ptr<const snapshot::Mesh> mesh;
        std::optional<gp_Pnt> point;
    };
    using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
    struct Items {
        TopoDS_Shape shape;
        ShapeMap faces;
        ShapeMap edges;
        ShapeMap vertices;
        mutable std::map<int, std::vector<std::uint32_t>> outlines;
    };

    std::optional<Found> pickAt(QPointF position) const;
    const Items& itemsOf(const core::Body& body) const;
    void addOutline(const render::PickedItem& face, std::set<render::PickedItem>& outline) const;
    std::optional<Pick> pickOf(const Found& found) const;
    const core::Body* bodyOf(const render::PickedItem& item) const;
    std::shared_ptr<const snapshot::Mesh> meshOf(const render::PickedItem& item) const;
    void press(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
    void release(QPointF position, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    void move(QPointF position, Qt::MouseButtons buttons);
    void setHovered(std::optional<Found> found);
    void dropStale();
    void showHighlight();
    void showHover();
    void addHover(render::Highlight& highlight) const;
    void announce(void (PickSource::*signal)());

    render::RhiView* m_view;
    const DesignSession* m_session;
    Scene* m_scene;
    pick::SceneRays* m_rays;
    BandOverlay* m_band;
    core::Model m_shown;
    std::vector<core::BodyKey> m_highlightedBodies;
    std::optional<Found> m_hovered;
    std::vector<Found> m_selected;
    std::optional<QPointF> m_pressed;
    Qt::KeyboardModifiers m_pressModifiers;
    bool m_boxFromPress = false;
    bool m_boxing = false;
    bool m_selectedByBox = false;
    mutable std::map<core::BodyKey, Items> m_items;
};

} // namespace kalve137::app
