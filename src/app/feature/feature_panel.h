// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/view/scene.h"

#include "core/document.h"
#include "core/recompute.h"

#include <QString>
#include <QWidget>

#include <array>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::app {

class DesignSession;
class DistanceHandle;
class ValueEdit;

class FeaturePanel : public QWidget {
    Q_OBJECT

public:
    FeaturePanel(DesignSession& session, Scene& scene, viewer::OcctViewWidget* view, QWidget* parent = nullptr);

    void startExtrude();
    void startRevolve();
    void startFillet();
    void startChamfer();
    void startShell();
    void startPattern();
    void startPressPull();
    void startHole();
    void startMove();
    void startAppearance();
    void startAppearanceOn(std::vector<core::BodyKey> bodies);
    void startOffsetPlane();
    void startPlaneAtAngle();
    void startMidplane();
    void startAxis();
    void startPoint();
    void setHandleIncrements(double length, double angle);
    void edit(core::FeatureId id);

    bool isActive() const { return m_data.has_value(); }
    void refreshPreview();
    void accept();
    void cancel();

signals:
    void activeChanged(bool active);
    void message(const QString& text);
    void selectionSummary(const QString& text);

private:
    struct Resolved {
        std::set<std::pair<core::FeatureId, std::size_t>> profiles;
        std::vector<TopoDS_Edge> edges;
        std::vector<TopoDS_Face> faces;
        TopoDS_Shape lastEdgeBody;
        std::vector<gp_Ax1> holeAxes;
        std::vector<QString> items;
        bool complete = false;
    };

    struct PlaneSource {
        gp_Ax3 frame;
        std::string name;
        gp_Pnt center;
    };

    void start(core::FeatureData data, core::FeatureId editing);
    void startFresh(core::FeatureData featureData);
    void finish();
    void buildForm();
    void buildOffsetFaceForm(core::OffsetFaceFeature& f, QFormLayout* fields);
    void buildHoleForm(core::HoleFeature& f, QFormLayout* fields);
    void buildMoveForm(core::MoveFeature& f, QFormLayout* fields);
    void buildAppearanceForm(core::AppearanceFeature& f, QFormLayout* fields);
    void buildPlaneForm(core::PlaneFeature& f, QFormLayout* fields);
    void updatePreview();
    void schedulePreview();
    Resolved resolve() const;
    void setExtrudeDistance(double distance);
    void onHandleValue(double value);
    void onSecondHandleValue(double value);
    void onThirdHandleValue(double value);
    void updateHandles(const Resolved& resolved);
    void onSelectionChanged();
    void togglePick(const Pick& pick);
    bool handOffPressPull(const Pick& pick);
    void toggleProfile(core::ProfileRef& ref, const Pick& pick);
    void toggleEdge(std::vector<core::TopoRef>& refs, const Pick& pick);
    void toggleEdgeRun(std::vector<core::TopoRef>& refs, std::size_t bodyIndex, const TopoDS_Edge& edge);
    void toggleFace(std::vector<core::TopoRef>& refs, const Pick& pick);
    void toggleHolePosition(core::HoleFeature& hole, const Pick& pick);
    void toggleBody(core::MoveFeature& move, const Pick& pick);
    void toggleAppearanceTarget(core::AppearanceFeature& appearance, const Pick& pick);
    bool pickAxisRef(core::AxisRef& axis, const Pick& pick);
    std::optional<PlaneSource> planeSourceOf(const Pick& pick);
    void pickPlaneBase(core::PlaneFeature& plane, const Pick& pick);
    void pickHinge(core::PlaneFeature& plane, const Pick& pick);
    void pickMidplane(core::PlaneFeature& plane, const Pick& pick);
    void pickAxisSource(core::AxisFeature& axis, const Pick& pick);
    void pickPointSource(core::PointFeature& point, const Pick& pick);
    void pickExtrudeTarget(core::ExtrudeFeature& extrude, const Pick& pick);
    void setPickingTarget(bool picking);
    QString extrudeTargetName(const core::ExtrudeFeature& extrude) const;
    void resolveHole(const core::HoleFeature& hole, Resolved& resolved) const;
    void resolvePlane(const core::PlaneFeature& plane, Resolved& resolved) const;
    std::vector<TopoDS_Shape> highlightedBodies(const core::Model& model, core::FeatureId featureId) const;
    std::set<core::BodyKey> previewBodies(const core::Model& model) const;
    core::FilletSet& activeSet(core::FilletFeature& fillet);
    const core::FilletSet& activeSet(const core::FilletFeature& fillet) const;
    std::optional<std::pair<std::size_t, TopoDS_Edge>> blendSourceOf(const std::vector<core::TopoRef>& refs,
                                                                     const TopoDS_Shape& picked) const;
    void refreshItems(const std::vector<QString>& items);
    void removeItem(std::size_t index);
    void preselectProfile(core::ProfileRef& ref) const;
    core::AxisRef defaultAxis(const core::ProfileRef& ref) const;
    core::Document candidate() const;
    QString selectionText() const;
    QString summaryOf(const Resolved& resolved) const;

    DesignSession& m_session;
    Scene& m_scene;
    viewer::OcctViewWidget* m_view;

    std::optional<core::FeatureData> m_data;
    core::FeatureId m_editing = 0;
    core::Document m_base;
    core::Model m_baseModel;

    QVBoxLayout* m_layout = nullptr;
    QWidget* m_form = nullptr;
    QLabel* m_selection = nullptr;
    QWidget* m_items = nullptr;
    QVBoxLayout* m_itemsLayout = nullptr;
    QLabel* m_error = nullptr;
    QPushButton* m_ok = nullptr;
    QComboBox* m_axis = nullptr;

    ValueEdit* m_distance = nullptr;
    ValueEdit* m_secondDistance = nullptr;
    QPushButton* m_flip = nullptr;
    ValueEdit* m_size = nullptr;
    std::array<ValueEdit*, 3> m_offsets{};
    bool m_tangentChain = true;
    std::size_t m_filletSet = 0;
    bool m_pressPull = false;
    bool m_appearanceFaces = false;
    bool m_pickingTarget = false;
    bool m_addOnly = false;
    QPushButton* m_target = nullptr;
    std::optional<gp_Pnt> m_firstAxisPoint;

    DistanceHandle* m_handle = nullptr;
    DistanceHandle* m_secondHandle = nullptr;
    DistanceHandle* m_thirdHandle = nullptr;
    QTimer* m_previewTimer = nullptr;
};

} // namespace kalve137::app
