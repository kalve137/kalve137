// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/view/pick_source.h"
#include "core/document.h"
#include "render/chunk_set.h"
#include "render/pick_id.h"

#include <QPointF>

#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Shape.hxx>

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace kalve137::core {
struct Body;
}

namespace kalve137::snapshot {
struct Mesh;
}

namespace kalve137::render {
class RhiView;
}

namespace kalve137::app {

class DesignSession;
class Scene;

class NewViewPicker : public PickSource {
    Q_OBJECT

public:
    NewViewPicker(render::RhiView& view, const DesignSession& session, Scene& scene, QObject* parent = nullptr);

    std::optional<Pick> hovered() const override;
    std::vector<Pick> selected() const override;
    std::vector<Pick> takePicks() override;
    bool selectHovered() override;
    void clearSelection() override;

    void hoverAt(QPointF position);
    void clickAt(QPointF position, Qt::KeyboardModifiers modifiers);
    render::PickKinds kinds() const;

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
    };

    std::optional<Found> pickAt(QPointF position) const;
    std::optional<Pick> pickOf(const Found& found) const;
    const core::Body* bodyOf(const render::PickedItem& item) const;
    void press(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons);
    void release(QPointF position, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    void move(QPointF position, Qt::MouseButtons buttons);
    void setHovered(std::optional<Found> found);
    void dropStale();
    void showHighlight();
    void announce(void (PickSource::*signal)());

    render::RhiView* m_view;
    const DesignSession* m_session;
    Scene* m_scene;
    std::optional<Found> m_hovered;
    std::vector<Found> m_selected;
    std::optional<QPointF> m_pressed;
    mutable std::map<core::BodyKey, Items> m_items;
};

} // namespace kalve137::app
