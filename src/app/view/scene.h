// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/document.h"
#include "core/recompute.h"

#include <AIS_DisplayMode.hxx>
#include <AIS_InteractiveObject.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace kalve137::geometry {
class ViewBlockers;
}

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::app {

class DesignSession;

struct Pick {
    enum class Kind {
        OriginPlane,
        Face,
        Edge,
        Vertex,
        Profile,
        SketchLine,
        SketchPoint,
        ConstructionPlane,
        OriginAxis,
        ConstructionAxis,
        ConstructionPoint,
    };

    Kind kind = Kind::Face;
    core::OriginPlane plane = core::OriginPlane::XY;
    core::ConstructionPlane construction;
    core::AxisKind axis = core::AxisKind::X;
    gp_Ax1 line;
    core::FeatureId feature = 0;
    int index = -1;
    TopoDS_Shape shape;
    std::optional<gp_Pnt> point;

    core::BodyKey body() const { return {feature, index}; }
};

enum class PickMode {
    None,
    Planes,
    Profiles,
    ProfilesAndLines,
    Edges,
    Faces,
    Measure,
    PressPull,
    HolePlacement,
    Lines,
    AxisSources,
    PointSources,
};

enum class VisualStyle { Shaded, ShadedWithEdges, Wireframe, VisibleEdges };

std::optional<gp_Ax3> sketchPlaneOf(const Pick& pick);

class Scene {
public:
    explicit Scene(viewer::OcctViewWidget* view);
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    void show(const core::Document& document, const core::Model& model, const DesignSession& session);
    void setPickMode(PickMode mode);
    PickMode pickMode() const { return m_mode; }
    void setEditingSketch(core::FeatureId sketch);
    void setVisualStyle(VisualStyle style);
    VisualStyle visualStyle() const { return m_style; }
    void setHighlightedProfiles(std::set<std::pair<core::FeatureId, std::size_t>> profiles);
    void setHighlightedEdges(std::vector<TopoDS_Edge> edges);
    void setHighlightedFaces(std::vector<TopoDS_Face> faces);
    void setHighlightedBodies(std::vector<TopoDS_Shape> shapes);
    void setPreviewBodies(std::set<core::BodyKey> keys);

    std::vector<Pick> takePicks();
    std::optional<Pick> hovered() const;
    std::vector<Pick> selected() const;
    bool selectHovered();
    void clearSelection();
    std::vector<Pick> takePreselection() { return std::exchange(m_preselection, {}); }

private:
    struct BodyObject {
        TopoDS_Shape shape;
        Handle(AIS_InteractiveObject) object;
        std::optional<geometry::Color> color;
        geometry::FaceColors faceColors;
        bool preview = false;
    };

    void rebuild();
    void updateBoxSelection();
    bool isVisible(const TopoDS_Shape& shape, const geometry::ViewBlockers& blockers) const;
    void dropBodies();
    std::size_t showBodies();
    void showSketches();
    void showHighlights();
    void showOrigin();
    void showConstructionPlanes();
    void showConstructionAxes();
    void showConstructionPoints();
    std::optional<Pick> pickOf(const Handle(SelectMgr_EntityOwner)& owner) const;
    void display(const Handle(AIS_InteractiveObject)& object, int selectionMode, std::optional<Pick> pick,
                 int displayMode = AIS_Shaded);

    viewer::OcctViewWidget* m_view;
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
    std::vector<TopAbs_ShapeEnum> m_bodySelection;
    std::map<const AIS_InteractiveObject*, Pick> m_picks;
    std::vector<Pick> m_preselection;
};

} // namespace kalve137::app
