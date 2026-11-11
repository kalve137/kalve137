// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/diagnostics/diagnostics.h"
#include "app/session/preferences.h"
#include "app/sketch/sketch_editor.h"

#include "core/feature.h"
#include "core/project.h"
#include "core/shape_store.h"

#include <QMainWindow>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QActionGroup;
class QDockWidget;
class QFrame;
class QLabel;
class QMenu;
class QToolBar;

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::app {

class Autosave;
class BrowserPanel;
class CommandSearch;
class DesignSession;
class FeaturePanel;
class MeasurePanel;
class ProjectPanel;
class Scene;
class SketchPalette;
class TimelineWidget;
class ViewControls;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openDesignAt(const std::filesystem::path& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void createMenus();
    void createToolBars();
    void createDocks();
    void createOverlays();
    void updateActions();
    void updateTitle();
    void updateStatus();
    void refreshScene();

    void startCreateSketch();
    void stopPickingPlane();
    void leaveModes();
    void createSketchOn(const gp_Ax3& plane, const std::string& planeName, core::FeatureId planeFeature = 0,
                        std::optional<core::TopoRef> face = std::nullopt);
    void fitView();
    void beginSketch(core::FeatureId id);
    void finishSketch();
    void editFeature(core::FeatureId id);
    void escape();
    void onViewSelection();
    void onViewHover();

    void restoreSession();
    void offerRecovery();
    void showVersionHistory();
    bool maybeSave();
    bool save();
    bool saveAs();
    bool writeDesign(const std::filesystem::path& path);
    core::ShapeStore& shapesOf(const std::filesystem::path& design);
    void newDesign();
    void openDesign();
    void newProject();
    void openProject();
    void openProjectAt(const std::filesystem::path& folder);
    void setProject(std::optional<core::Project> project);
    void updateRecentProjects();
    void importStep();
    void exportStep();
    void exportStl();
    std::filesystem::path exportPath(const QString& extension, const QString& filter);

    void trackCommand(QAction* action);
    void repeatLastCommand();
    void updateRepeatAction();
    void showMarkingMenu(QPoint center);
    QString idleSelectionSummary() const;

    void createHelpMenu();
    void offerCrashReport();
    void showProblemReport(std::optional<diagnostics::Crash> crash = std::nullopt);

    void showAbout();
    void showPreferences();
    void showGridSettings();
    void applyPreferences();
    void lookAtSketch();

    static constexpr int kMessageTimeoutMs = 5000;

    Preferences m_preferences;
    DesignSession* m_session = nullptr;
    Autosave* m_autosave = nullptr;
    viewer::OcctViewWidget* m_view = nullptr;
    std::unique_ptr<Scene> m_scene;
    SketchEditor* m_sketchEditor = nullptr;
    FeaturePanel* m_featurePanel = nullptr;
    TimelineWidget* m_timeline = nullptr;
    BrowserPanel* m_browser = nullptr;
    ProjectPanel* m_projectPanel = nullptr;
    QFrame* m_featureFrame = nullptr;
    MeasurePanel* m_measurePanel = nullptr;
    QFrame* m_measureFrame = nullptr;
    SketchPalette* m_sketchPalette = nullptr;
    QFrame* m_paletteFrame = nullptr;
    ViewControls* m_viewControls = nullptr;
    QToolBar* m_sketchBar = nullptr;
    QLabel* m_selectionStatus = nullptr;
    QLabel* m_sketchStatus = nullptr;
    QMenu* m_recentMenu = nullptr;

    std::optional<core::Project> m_project;
    core::ShapeStore m_shapes;
    bool m_pickingPlane = false;

    QAction* m_undo = nullptr;
    QAction* m_redo = nullptr;
    QAction* m_createSketch = nullptr;
    QAction* m_extrude = nullptr;
    QAction* m_revolve = nullptr;
    QAction* m_fillet = nullptr;
    QAction* m_chamfer = nullptr;
    QAction* m_shell = nullptr;
    QAction* m_pressPull = nullptr;
    QAction* m_hole = nullptr;
    QAction* m_move = nullptr;
    QAction* m_appearance = nullptr;
    QAction* m_pattern = nullptr;
    QAction* m_offsetPlane = nullptr;
    QAction* m_planeAtAngle = nullptr;
    QAction* m_midplane = nullptr;
    QAction* m_axis = nullptr;
    QAction* m_point = nullptr;
    QAction* m_measure = nullptr;
    QAction* m_finishSketch = nullptr;
    QAction* m_construction = nullptr;
    QAction* m_delete = nullptr;
    QAction* m_showOrigin = nullptr;
    QAction* m_repeat = nullptr;
    CommandSearch* m_commandSearch = nullptr;
    std::vector<QAction*> m_recentCommands;
    QActionGroup* m_sketchTools = nullptr;
    std::map<SketchEditor::Tool, QAction*> m_toolActions;
};

} // namespace kalve137::app
