// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/view/pick.h"
#include "app/view/pick_source.h"
#include "core/document.h"
#include "core/recompute.h"

#include <AIS_DisplayMode.hxx>
#include <AIS_InteractiveObject.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kalve137::geometry {
class ViewBlockers;
}

namespace kalve137::core {
class SnapshotCache;
}

namespace kalve137::pick {
class SceneRays;
}

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::app {

class DesignSession;

enum class VisualStyle { Shaded, ShadedWithEdges, Wireframe, VisibleEdges };

class Scene : public PickSource {
    Q_OBJECT

public:
    Scene(viewer::OcctViewWidget* view, core::SnapshotCache& snapshots, pick::SceneRays& rays);
    ~Scene() override;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    void show(const core::Document& document, const core::Model& model, const DesignSession& session);
    void setPicks(PickSource& picks) { m_source = &picks; }
    bool setBodiesVisible(const std::vector<core::BodyKey>& keys, bool visible);
    std::size_t displayedBodies() const { return m_bodies.size(); }
    void setPickMode(PickMode mode);
    PickMode pickMode() const { return m_mode; }
    core::FeatureId editingSketch() const { return m_editingSketch; }
    void setEditingSketch(core::FeatureId sketch);
    void setVisualStyle(VisualStyle style);
    VisualStyle visualStyle() const { return m_style; }
    void restyle();
    void setHighlightedProfiles(std::set<std::pair<core::FeatureId, std::size_t>> profiles);
    void setHighlightedEdges(std::vector<TopoDS_Edge> edges);
    void setHighlightedFaces(std::vector<TopoDS_Face> faces);
    void setHighlightedBodies(std::vector<TopoDS_Shape> shapes);
    void setPreviewBodies(std::set<core::BodyKey> keys);

    std::vector<Pick> takePicks() override;
    std::optional<Pick> hovered() const override;
    std::vector<Pick> selected() const override;
    bool selectHovered() override;
    void clearSelection() override;
    bool selectedByBox() const override;
    std::vector<Pick> takePreselection() { return std::exchange(m_preselection, {}); }

signals:
    void pickModeChanged();

private:
    struct BodyObject {
        core::BodyKey key;
        TopoDS_Shape shape;
        Handle(AIS_InteractiveObject) object;
        std::optional<geometry::Color> color;
        geometry::FaceColors faceColors;
        bool preview = false;
        int displayMode = AIS_Shaded;
    };

    void rebuild();
    void updateBoxSelection();
    bool isVisible(const TopoDS_Shape& shape, const geometry::ViewBlockers& blockers) const;
    void dropBodies();
    void dropErased();
    void displayBody(const BodyObject& body);
    std::size_t showBodies();
    void showSketches();
    void showHighlights();
    void showOrigin();
    void showConstructionPlanes();
    void showConstructionAxes();
    void showConstructionPoints();
    using PickedPoints = std::unordered_map<const SelectMgr_EntityOwner*, gp_Pnt>;

    std::optional<Pick> pickOf(const Handle(SelectMgr_EntityOwner)& owner) const;
    std::optional<Pick> pickWithPoint(const Handle(SelectMgr_EntityOwner)& owner, const PickedPoints& points) const;
    PickedPoints pickedPoints() const;
    void display(const Handle(AIS_InteractiveObject)& object, int selectionMode, std::optional<Pick> pick,
                 int displayMode = AIS_Shaded);

    viewer::OcctViewWidget* m_view;
    core::SnapshotCache* m_snapshots;
    pick::SceneRays* m_rays;
    PickSource* m_source = this;
    core::Document m_document;
    core::Model m_model;
    std::set<core::FeatureId> m_hidden;
    std::set<core::BodyKey> m_hiddenBodies;
    std::set<core::BodyKey> m_previewBodies;
    bool m_originVisible = false;
    PickMode m_mode = PickMode::None;
    VisualStyle m_style = VisualStyle::ShadedWithEdges;
    core::FeatureId m_editingSketch = 0;
    std::set<std::pair<core::FeatureId, std::size_t>> m_highlightedProfiles;
    std::vector<TopoDS_Edge> m_highlightedEdges;
    std::vector<TopoDS_Face> m_highlightedFaces;
    std::vector<TopoDS_Shape> m_highlightedBodies;

    std::vector<Handle(AIS_InteractiveObject)> m_objects;
    std::vector<BodyObject> m_bodies;
    std::vector<BodyObject> m_erased;
    std::vector<TopAbs_ShapeEnum> m_bodySelection;
    std::map<const AIS_InteractiveObject*, Pick> m_picks;
    std::vector<Pick> m_preselection;
};

} // namespace kalve137::app
