// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/main_window.h"

#include "app/diagnostics/diagnostics.h"
#include "app/feature/feature_panel.h"
#include "app/feature/measure_panel.h"
#include "app/panels/browser_panel.h"
#include "app/panels/project_panel.h"
#include "app/panels/properties_dialog.h"
#include "app/panels/timeline_widget.h"
#include "app/session/autosave.h"
#include "app/session/design_session.h"
#include "app/session/paths.h"
#include "app/session/preferences_dialog.h"
#include "app/shell/command_search.h"
#include "app/shell/modal_scrim.h"
#include "app/shell/notices_dialog.h"
#include "app/shell/shortcuts_dialog.h"
#include "app/sketch/sketch_palette.h"
#include "app/theme/fonts.h"
#include "app/theme/icons.h"
#include "app/theme/theme.h"
#include "app/view/grid_settings.h"
#include "app/view/scene.h"
#include "app/view/view_controls.h"
#include "app/view/view_cube_arrows.h"
#include "app/view/view_overlay.h"
#include "core/recompute.h"

#include "platform/platform.h"
#include "viewer/occt_view_widget.h"

#include <OcctQtTools.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QSvgRenderer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <initializer_list>
#include <utility>
#include <variant>
#include <vector>

namespace kalve137::app {

namespace {

constexpr int kToolIconSize = 32;
constexpr double kAboutWordmarkHeight = 48.0;
constexpr int kFeaturePanelWidth = theme::kWidthNarrow;
constexpr QPoint kFeaturePanelMargin{12, 12};
constexpr QPoint kNavBarMargin{0, 10};
const char* const kWindowGeometryKey = "windowGeometry";

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    m_session = new DesignSession(this);
    m_autosave = new Autosave(*m_session, this);

    m_view = new viewer::OcctViewWidget(this);
    m_view->setBackgroundColor(theme::kCanvas);
    m_view->setGridStyle({
        .background = OcctQtTools::qtColorToOcct(theme::kCanvas),
        .minor = OcctQtTools::qtColorToOcct(theme::kGridMinor),
        .major = OcctQtTools::qtColorToOcct(theme::kGridMajor),
        .axisX = OcctQtTools::qtColorToOcct(theme::kAxisX),
        .axisY = OcctQtTools::qtColorToOcct(theme::kAxisY),
        .axisZ = OcctQtTools::qtColorToOcct(theme::kAxisZ),
        .label = OcctQtTools::qtColorToOcct(theme::kGridLabel),
        .labelFont = fonts::families().occtRegular.toStdString(),
    });
    m_view->setHighlightColors(theme::kHover, theme::kGold);
    m_view->setViewCubeStyle({
        .face = theme::kSlateLight,
        .edge = theme::kSlateEdge,
        .label = theme::kTitaniumLight,
        .hover = theme::kGold,
        .axisX = theme::kAxisX,
        .axisY = theme::kAxisY,
        .axisZ = theme::kAxisZ,
        .labelFont = fonts::families().occtMedium,
    });
    setCentralWidget(m_view);

    m_scene = std::make_unique<Scene>(m_view);
    m_sketchEditor = new SketchEditor(*m_session, m_view, this);

    createActions();
    createDocks();
    createOverlays();
    createMenus();
    createToolBars();

    m_selectionStatus = new QLabel(this);
    statusBar()->addPermanentWidget(m_selectionStatus);
    m_sketchStatus = new QLabel(this);
    statusBar()->addPermanentWidget(m_sketchStatus);
    new ModalScrim(this);

    if (!restoreGeometry(QSettings().value(kWindowGeometryKey).toByteArray()) ||
        QGuiApplication::screenAt(geometry().center()) == nullptr) {
        resize(sizeHint());
        move(QGuiApplication::primaryScreen()->availableGeometry().center() - rect().center());
    }

    connect(m_session, &DesignSession::changed, this, [this] {
        m_featurePanel->cancel();
        refreshScene();
        updateActions();
    });
    connect(m_session, &DesignSession::visibilityChanged, this, &MainWindow::refreshScene);
    connect(m_session, &DesignSession::fileStateChanged, this, &MainWindow::updateTitle);
    connect(m_view, &viewer::OcctViewWidget::ready, this, [this] {
        diagnostics::setGlInfo(m_view->glInfo());
        refreshScene();
        fitView();
    });
    connect(m_view, &viewer::OcctViewWidget::selectionChanged, this, &MainWindow::onViewSelection);
    connect(m_view, &viewer::OcctViewWidget::hoverChanged, this, &MainWindow::onViewHover);
    connect(m_view, &viewer::OcctViewWidget::menuRequested, this, &MainWindow::showMarkingMenu);
    connect(
        m_view, &viewer::OcctViewWidget::frameFailed, this,
        [this](bool redrawStopped) {
            statusBar()->showMessage(redrawStopped ? tr("The view could not be drawn, move the mouse to retry")
                                                   : tr("The view hit a drawing error and was reset"),
                                     kMessageTimeoutMs);
        },
        Qt::QueuedConnection);

    connect(m_sketchEditor, &SketchEditor::toolChanged, this, [this] {
        if (auto it = m_toolActions.find(m_sketchEditor->tool()); it != m_toolActions.end()) {
            it->second->setChecked(true);
        }
        updateActions();
        updateStatus();
    });
    connect(m_sketchEditor, &SketchEditor::stateChanged, this, &MainWindow::updateStatus);
    connect(m_sketchEditor, &SketchEditor::message, this,
            [this](const QString& text) { statusBar()->showMessage(text, kMessageTimeoutMs); });
    connect(m_sketchEditor, &SketchEditor::finished, this, &MainWindow::finishSketch);

    connect(m_featurePanel, &FeaturePanel::activeChanged, this, [this](bool active) {
        m_featureFrame->setVisible(active);
        if (!active) {
            refreshScene();
        }
        updateActions();
    });
    connect(m_measurePanel, &MeasurePanel::activeChanged, this, [this](bool active) {
        m_measureFrame->setVisible(active);
        updateActions();
    });
    connect(m_featurePanel, &FeaturePanel::message, this,
            [this](const QString& text) { statusBar()->showMessage(text, kMessageTimeoutMs); });
    connect(m_featurePanel, &FeaturePanel::selectionSummary, m_selectionStatus, &QLabel::setText);

    m_preferences = Preferences::load();
    applyPreferences();

    restoreSession();
    updateActions();
    updateTitle();
    updateStatus();
}

MainWindow::~MainWindow()
{
    m_measurePanel->stop();
    m_featurePanel->cancel();
    m_sketchEditor->end();
    delete m_featureFrame;
    delete m_measureFrame;
}

void MainWindow::createActions()
{
    m_undo = new QAction(icons::tinted("undo"), tr("&Undo"), this);
    m_undo->setShortcut(QKeySequence::Undo);
    connect(m_undo, &QAction::triggered, this, [this] {
        stopPickingPlane();
        m_measurePanel->stop();
        m_featurePanel->cancel();
        m_session->undo();
    });
    m_redo = new QAction(icons::tinted("redo"), tr("&Redo"), this);
    m_redo->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    connect(m_redo, &QAction::triggered, this, [this] {
        stopPickingPlane();
        m_measurePanel->stop();
        m_featurePanel->cancel();
        m_session->redo();
    });

    m_createSketch = new QAction(icons::tinted("create_sketch"), tr("Create Sketch"), this);
    m_createSketch->setToolTip(tr("Create Sketch: pick an origin plane or a flat face"));
    connect(m_createSketch, &QAction::triggered, this, &MainWindow::startCreateSketch);

    const auto featureAction = [this](const QString& icon, const QString& text, const QKeySequence& key,
                                      void (FeaturePanel::*start)()) {
        auto* action = new QAction(icons::tinted(icon), text, this);
        action->setShortcut(key);
        if (!key.isEmpty()) {
            action->setToolTip(tr("%1 (%2)").arg(text, key.toString(QKeySequence::NativeText)));
        }
        connect(action, &QAction::triggered, this, [this, start] {
            if (m_sketchEditor->isActive()) {
                finishSketch();
            }
            stopPickingPlane();
            m_measurePanel->stop();
            m_featurePanel->cancel();
            (m_featurePanel->*start)();
        });
        return action;
    };
    m_extrude = featureAction("extrude", tr("Extrude"), QKeySequence(Qt::Key_E), &FeaturePanel::startExtrude);
    m_revolve = featureAction("revolve", tr("Revolve"), QKeySequence(), &FeaturePanel::startRevolve);
    m_fillet = featureAction("fillet", tr("Fillet"), QKeySequence(Qt::Key_F), &FeaturePanel::startFillet);
    m_chamfer = featureAction("chamfer", tr("Chamfer"), QKeySequence(), &FeaturePanel::startChamfer);
    m_shell = featureAction("shell", tr("Shell"), QKeySequence(), &FeaturePanel::startShell);
    m_pressPull = featureAction("press_pull", tr("Press Pull"), QKeySequence(Qt::Key_Q), &FeaturePanel::startPressPull);
    m_pressPull->setToolTip(tr("Press Pull (Q): extrudes a profile, offsets a flat face or fillets an edge"));
    m_hole = featureAction("hole", tr("Hole"), QKeySequence(), &FeaturePanel::startHole);
    m_hole->setToolTip(tr("Hole: simple, counterbore or countersink holes on faces or sketch points"));
    m_move = featureAction("move", tr("Move"), QKeySequence(Qt::Key_M), &FeaturePanel::startMove);
    m_move->setToolTip(tr("Move (M): moves, turns or copies bodies"));
    m_appearance =
        featureAction("appearance", tr("Appearance"), QKeySequence(Qt::Key_A), &FeaturePanel::startAppearance);
    m_appearance->setToolTip(tr("Appearance (A): colors whole bodies or single faces"));
    m_pattern = featureAction("pattern", tr("Pattern"), QKeySequence(), &FeaturePanel::startPattern);
    m_pattern->setToolTip(tr("Pattern: copies a body along an axis or around it"));
    m_offsetPlane = featureAction("offset_plane", tr("Offset Plane"), QKeySequence(), &FeaturePanel::startOffsetPlane);
    m_offsetPlane->setToolTip(tr("Offset Plane: a plane parallel to an origin plane or a flat face"));
    m_planeAtAngle =
        featureAction("plane_angle", tr("Plane at Angle"), QKeySequence(), &FeaturePanel::startPlaneAtAngle);
    m_planeAtAngle->setToolTip(tr("Plane at Angle: a plane turned about a line, edge or axis"));
    m_midplane = featureAction("midplane", tr("Midplane"), QKeySequence(), &FeaturePanel::startMidplane);
    m_midplane->setToolTip(tr("Midplane: a plane halfway between two planes or flat faces"));
    m_axis = featureAction("axis", tr("Axis"), QKeySequence(), &FeaturePanel::startAxis);
    m_axis->setToolTip(tr("Axis: along an edge, through a cylinder, normal to a face or through two points"));
    m_point = featureAction("construct_point", tr("Point"), QKeySequence(), &FeaturePanel::startPoint);
    m_point->setToolTip(tr("Point: at a vertex, an edge midpoint, a circle center or a face center"));

    m_measure = new QAction(icons::tinted("measure"), tr("Measure"), this);
    m_measure->setShortcut(QKeySequence(Qt::Key_I));
    m_measure->setToolTip(tr("Measure (I): sizes of a face, edge or vertex, or the distance between two"));
    connect(m_measure, &QAction::triggered, this, [this] {
        stopPickingPlane();
        m_featurePanel->cancel();
        m_measurePanel->start();
    });

    m_finishSketch = new QAction(icons::tinted("finish_sketch", theme::kSlateDeep), tr("Finish Sketch"), this);
    connect(m_finishSketch, &QAction::triggered, this, &MainWindow::finishSketch);

    m_construction = new QAction(icons::tinted("construction"), tr("Construction"), this);
    m_construction->setShortcut(QKeySequence(Qt::Key_X));
    m_construction->setToolTip(tr("Toggle construction on the selected curves (X)"));
    connect(m_construction, &QAction::triggered, m_sketchEditor, &SketchEditor::toggleConstruction);

    m_delete = new QAction(tr("Delete"), this);
    m_delete->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    connect(m_delete, &QAction::triggered, m_sketchEditor, &SketchEditor::deleteSelection);

    m_showOrigin = new QAction(tr("Show Origin"), this);
    m_showOrigin->setCheckable(true);
    connect(m_showOrigin, &QAction::toggled, m_session, &DesignSession::setOriginVisible);
    connect(m_session, &DesignSession::visibilityChanged, this, [this] {
        const QSignalBlocker blocker(m_showOrigin);
        m_showOrigin->setChecked(m_session->isOriginVisible());
    });

    m_viewControls = new ViewControls(m_preferences, m_showOrigin, this);
    connect(m_viewControls, &ViewControls::homeRequested, this, [this] {
        m_view->animateCamera([this] {
            m_view->homeView();
            fitView();
        });
    });
    connect(m_viewControls, &ViewControls::standardViewRequested, this, [this](viewer::StandardView view) {
        m_view->animateCamera([this, view] {
            m_view->standardView(view);
            fitView();
        });
    });
    connect(m_viewControls, &ViewControls::fitRequested, this,
            [this] { m_view->animateCamera([this] { fitView(); }); });
    connect(m_viewControls, &ViewControls::lookAtRequested, this, &MainWindow::lookAtSketch);
    connect(m_viewControls, &ViewControls::gridSettingsRequested, this, &MainWindow::showGridSettings);
    connect(m_viewControls, &ViewControls::preferencesEdited, this, [this] {
        m_preferences.save();
        applyPreferences();
    });

    auto* escapeAction = new QAction(this);
    escapeAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(escapeAction, &QAction::triggered, this, &MainWindow::escape);
    addAction(escapeAction);

    struct ToolDefinition {
        SketchEditor::Tool tool;
        const char* icon;
        QString text;
        QKeySequence key;
    };
    const ToolDefinition tools[] = {
        {SketchEditor::Tool::Select, "select", tr("Select"), {}},
        {SketchEditor::Tool::Line, "line", tr("Line"), QKeySequence(Qt::Key_L)},
        {SketchEditor::Tool::Rectangle, "rectangle", tr("Rectangle"), QKeySequence(Qt::Key_R)},
        {SketchEditor::Tool::CenterRectangle, "center_rectangle", tr("Center Rectangle"), {}},
        {SketchEditor::Tool::Circle, "circle", tr("Circle"), QKeySequence(Qt::Key_C)},
        {SketchEditor::Tool::ArcThrough, "arc_through", tr("3-Point Arc"), QKeySequence(Qt::Key_A)},
        {SketchEditor::Tool::Arc, "arc", tr("Center Point Arc"), {}},
        {SketchEditor::Tool::Polygon, "polygon", tr("Polygon"), {}},
        {SketchEditor::Tool::Point, "point", tr("Point"), {}},
        {SketchEditor::Tool::Trim, "trim", tr("Trim"), QKeySequence(Qt::Key_T)},
        {SketchEditor::Tool::Fillet, "sketch_fillet", tr("Fillet"), {}},
        {SketchEditor::Tool::Mirror, "mirror", tr("Mirror"), QKeySequence(Qt::Key_M)},
        {SketchEditor::Tool::Dimension, "dimension", tr("Dimension"), QKeySequence(Qt::Key_D)},
        {SketchEditor::Tool::Coincident, "coincident", tr("Coincident"), {}},
        {SketchEditor::Tool::Horizontal, "horizontal", tr("Horizontal"), {}},
        {SketchEditor::Tool::Vertical, "vertical", tr("Vertical"), {}},
        {SketchEditor::Tool::Parallel, "parallel", tr("Parallel"), {}},
        {SketchEditor::Tool::Perpendicular, "perpendicular", tr("Perpendicular"), {}},
        {SketchEditor::Tool::Tangent, "tangent", tr("Tangent"), {}},
        {SketchEditor::Tool::Equal, "equal", tr("Equal"), {}},
        {SketchEditor::Tool::Midpoint, "midpoint", tr("Midpoint"), {}},
        {SketchEditor::Tool::Symmetric, "symmetric", tr("Symmetric"), {}},
        {SketchEditor::Tool::Fix, "lock", tr("Fix"), {}},
    };
    m_sketchTools = new QActionGroup(this);
    m_sketchTools->setExclusive(true);
    for (const ToolDefinition& definition : tools) {
        auto* action = new QAction(icons::tinted(definition.icon), definition.text, m_sketchTools);
        action->setCheckable(true);
        action->setShortcut(definition.key);
        if (!definition.key.isEmpty()) {
            action->setToolTip(tr("%1 (%2)").arg(definition.text, definition.key.toString(QKeySequence::NativeText)));
        }
        const SketchEditor::Tool tool = definition.tool;
        connect(action, &QAction::triggered, this, [this, tool] { m_sketchEditor->setTool(tool); });
        m_toolActions.emplace(tool, action);
    }

    m_repeat = new QAction(tr("Repeat"), this);
    m_repeat->setShortcut(QKeySequence(Qt::Key_Space));
    connect(m_repeat, &QAction::triggered, this, &MainWindow::repeatLastCommand);
    for (QAction* action :
         {m_createSketch, m_extrude, m_revolve, m_fillet, m_chamfer, m_shell, m_pressPull, m_hole, m_move, m_appearance,
          m_pattern, m_offsetPlane, m_planeAtAngle, m_midplane, m_axis, m_point, m_measure}) {
        trackCommand(action);
    }
    for (const auto& [tool, action] : m_toolActions) {
        if (tool != SketchEditor::Tool::Select) {
            trackCommand(action);
        }
    }
    updateRepeatAction();

    m_commandSearch = new CommandSearch(this);

    for (QAction* action : {m_undo, m_redo, m_extrude, m_fillet, m_pressPull, m_move, m_construction, m_delete,
                            m_repeat, m_commandSearch->openAction()}) {
        addAction(action);
    }
    for (QAction* action : m_sketchTools->actions()) {
        addAction(action);
    }
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("New &Project..."), this, &MainWindow::newProject);
    fileMenu->addAction(tr("Open P&roject..."), this, &MainWindow::openProject);
    m_recentMenu = fileMenu->addMenu(tr("Recent Projects"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&New Design"), QKeySequence::New, this, &MainWindow::newDesign);
    fileMenu->addAction(tr("&Open Design..."), QKeySequence::Open, this, &MainWindow::openDesign);
    fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
    fileMenu->addAction(tr("Save &As..."), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    fileMenu->addAction(tr("&Version History..."), this, &MainWindow::showVersionHistory);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Import STEP..."), this, &MainWindow::importStep);
    fileMenu->addAction(tr("Export S&TEP..."), this, &MainWindow::exportStep);
    fileMenu->addAction(tr("Export ST&L..."), this, &MainWindow::exportStl);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_undo);
    editMenu->addAction(m_redo);
    editMenu->addAction(m_repeat);
    editMenu->addAction(m_commandSearch->openAction());
    editMenu->addSeparator();
    editMenu->addAction(m_delete);
    editMenu->addSeparator();
    editMenu->addAction(tr("&Preferences..."), QKeySequence(Qt::CTRL | Qt::Key_Comma), this,
                        &MainWindow::showPreferences);

    QMenu* createMenu = menuBar()->addMenu(tr("&Create"));
    createMenu->addAction(m_createSketch);
    createMenu->addSeparator();
    createMenu->addAction(m_extrude);
    createMenu->addAction(m_revolve);
    createMenu->addAction(m_fillet);
    createMenu->addAction(m_chamfer);
    createMenu->addAction(m_shell);
    createMenu->addAction(m_hole);
    createMenu->addAction(m_pattern);
    createMenu->addSeparator();
    createMenu->addAction(m_pressPull);
    createMenu->addAction(m_move);
    createMenu->addAction(m_appearance);

    QMenu* constructMenu = menuBar()->addMenu(tr("C&onstruct"));
    constructMenu->addAction(m_offsetPlane);
    constructMenu->addAction(m_planeAtAngle);
    constructMenu->addAction(m_midplane);
    constructMenu->addSeparator();
    constructMenu->addAction(m_axis);
    constructMenu->addAction(m_point);

    QMenu* inspectMenu = menuBar()->addMenu(tr("&Inspect"));
    inspectMenu->addAction(m_measure);
    inspectMenu->addAction(tr("&Properties..."), this, [this] { showProperties(this, m_session->model()); });

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewControls->addToMenu(viewMenu);
    viewMenu->addSeparator();
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        viewMenu->addAction(dock->toggleViewAction());
    }

    createHelpMenu();
}

void MainWindow::createToolBars()
{
    const auto iconOnly = [](QToolBar* bar, QAction* action) {
        bar->addAction(action);
        if (auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(action))) {
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        }
    };
    const auto addGroup = [](QToolBar* bar, const std::vector<QAction*>& actions) {
        auto* button = new QToolButton(bar);
        button->setPopupMode(QToolButton::MenuButtonPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(bar->iconSize());
        auto* menu = new QMenu(button);
        for (QAction* action : actions) {
            menu->addAction(action);
            connect(action, &QAction::triggered, button, [button, action] { button->setDefaultAction(action); });
        }
        button->setMenu(menu);
        button->setDefaultAction(actions.front());
        bar->addWidget(button);
    };

    QToolBar* create = addToolBar(tr("Create"));
    create->setObjectName("createBar");
    create->setMovable(false);
    create->setIconSize(QSize(kToolIconSize, kToolIconSize));
    create->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    iconOnly(create, m_undo);
    iconOnly(create, m_redo);
    create->addSeparator();
    create->addAction(m_createSketch);
    create->addSeparator();
    create->addAction(m_extrude);
    create->addAction(m_revolve);
    create->addAction(m_fillet);
    create->addAction(m_chamfer);
    create->addAction(m_shell);
    create->addAction(m_hole);
    create->addAction(m_pattern);
    create->addSeparator();
    create->addAction(m_pressPull);
    create->addAction(m_move);
    create->addAction(m_appearance);
    create->addSeparator();
    addGroup(create, {m_offsetPlane, m_planeAtAngle, m_midplane, m_axis, m_point});
    create->addSeparator();
    create->addAction(m_measure);

    addToolBarBreak();
    m_sketchBar = addToolBar(tr("Sketch"));
    m_sketchBar->setObjectName("sketchBar");
    m_sketchBar->setMovable(false);
    m_sketchBar->setIconSize(QSize(kToolIconSize, kToolIconSize));
    m_sketchBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    const auto add = [this](SketchEditor::Tool tool) { m_sketchBar->addAction(m_toolActions.at(tool)); };
    const auto addConstraint = [this, iconOnly](SketchEditor::Tool tool) {
        iconOnly(m_sketchBar, m_toolActions.at(tool));
    };
    const auto addVariants = [this, addGroup](std::initializer_list<SketchEditor::Tool> variants) {
        std::vector<QAction*> actions;
        for (SketchEditor::Tool tool : variants) {
            actions.push_back(m_toolActions.at(tool));
        }
        addGroup(m_sketchBar, actions);
    };
    add(SketchEditor::Tool::Select);
    m_sketchBar->addSeparator();
    add(SketchEditor::Tool::Line);
    addVariants({SketchEditor::Tool::Rectangle, SketchEditor::Tool::CenterRectangle});
    add(SketchEditor::Tool::Circle);
    addVariants({SketchEditor::Tool::ArcThrough, SketchEditor::Tool::Arc});
    add(SketchEditor::Tool::Polygon);
    add(SketchEditor::Tool::Point);
    m_sketchBar->addSeparator();
    add(SketchEditor::Tool::Trim);
    add(SketchEditor::Tool::Fillet);
    add(SketchEditor::Tool::Mirror);
    m_sketchBar->addSeparator();
    add(SketchEditor::Tool::Dimension);
    m_sketchBar->addSeparator();
    addConstraint(SketchEditor::Tool::Coincident);
    addConstraint(SketchEditor::Tool::Horizontal);
    addConstraint(SketchEditor::Tool::Vertical);
    addConstraint(SketchEditor::Tool::Parallel);
    addConstraint(SketchEditor::Tool::Perpendicular);
    addConstraint(SketchEditor::Tool::Tangent);
    addConstraint(SketchEditor::Tool::Equal);
    addConstraint(SketchEditor::Tool::Midpoint);
    addConstraint(SketchEditor::Tool::Symmetric);
    addConstraint(SketchEditor::Tool::Fix);
    m_sketchBar->addSeparator();
    m_sketchBar->addAction(m_construction);
    m_sketchBar->addSeparator();
    m_sketchBar->addAction(m_finishSketch);
    if (auto* button = qobject_cast<QToolButton*>(m_sketchBar->widgetForAction(m_finishSketch))) {
        button->setObjectName("finishSketch");
    }
    m_sketchBar->hide();
}

void MainWindow::createDocks()
{
    m_projectPanel = new ProjectPanel(this);
    auto* projectDock = new QDockWidget(tr("Project"), this);
    projectDock->setObjectName("projectDock");
    projectDock->setWidget(m_projectPanel);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);
    connect(m_projectPanel, &ProjectPanel::openDesignRequested, this, [this](const std::filesystem::path& path) {
        if (maybeSave()) {
            openDesignAt(path);
        }
    });
    connect(m_projectPanel, &ProjectPanel::newDesignRequested, this, &MainWindow::newDesign);
    connect(m_projectPanel, &ProjectPanel::newProjectRequested, this, &MainWindow::newProject);
    connect(m_projectPanel, &ProjectPanel::openProjectRequested, this, &MainWindow::openProject);

    m_browser = new BrowserPanel(*m_session, this);
    auto* browserDock = new QDockWidget(tr("Browser"), this);
    browserDock->setObjectName("browserDock");
    browserDock->setWidget(m_browser);
    addDockWidget(Qt::LeftDockWidgetArea, browserDock);
    tabifyDockWidget(projectDock, browserDock);
    setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
    projectDock->setTitleBarWidget(new QWidget(projectDock));
    browserDock->setTitleBarWidget(new QWidget(browserDock));
    browserDock->raise();
    connect(m_browser, &BrowserPanel::editRequested, this, &MainWindow::editFeature);
    connect(m_browser, &BrowserPanel::appearanceRequested, this, [this](const std::vector<core::BodyKey>& bodies) {
        if (m_sketchEditor->isActive()) {
            finishSketch();
        }
        stopPickingPlane();
        m_measurePanel->stop();
        m_featurePanel->cancel();
        m_featurePanel->startAppearanceOn(bodies);
    });

    m_timeline = new TimelineWidget(*m_session, this);
    auto* area = new QScrollArea(this);
    area->setObjectName("timelineArea");
    area->setWidget(m_timeline);
    area->setWidgetResizable(true);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setFixedHeight(m_timeline->sizeHint().height() + 14);
    auto* timelineDock = new QDockWidget(tr("Timeline"), this);
    timelineDock->setObjectName("timelineDock");
    timelineDock->setWidget(area);
    timelineDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    timelineDock->setTitleBarWidget(new QWidget(timelineDock));
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock);
    connect(m_timeline, &TimelineWidget::editRequested, this, &MainWindow::editFeature);
}

void MainWindow::createOverlays()
{
    auto* overlay = new ViewOverlay(m_view);

    m_featureFrame = new QFrame;
    m_featureFrame->setObjectName("floatingPanel");
    m_featureFrame->setFixedWidth(kFeaturePanelWidth);
    m_featurePanel = new FeaturePanel(*m_session, *m_scene, m_view, m_featureFrame);
    auto* layout = new QVBoxLayout(m_featureFrame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_featurePanel);
    m_featureFrame->hide();
    overlay->pin(m_featureFrame, Qt::AlignTop | Qt::AlignRight, kFeaturePanelMargin);

    m_measureFrame = new QFrame;
    m_measureFrame->setObjectName("floatingPanel");
    m_measureFrame->setFixedWidth(kFeaturePanelWidth);
    m_measurePanel = new MeasurePanel(*m_scene, m_view, m_measureFrame);
    auto* measureLayout = new QVBoxLayout(m_measureFrame);
    measureLayout->setContentsMargins(0, 0, 0, 0);
    measureLayout->addWidget(m_measurePanel);
    m_measureFrame->hide();
    overlay->pin(m_measureFrame, Qt::AlignTop | Qt::AlignRight, kFeaturePanelMargin);

    m_paletteFrame = new QFrame;
    m_paletteFrame->setObjectName("floatingPanel");
    m_sketchPalette = new SketchPalette(m_paletteFrame);
    auto* paletteLayout = new QVBoxLayout(m_paletteFrame);
    paletteLayout->setContentsMargins(0, 0, 0, 0);
    paletteLayout->addWidget(m_sketchPalette);
    m_paletteFrame->hide();
    overlay->pin(m_paletteFrame, Qt::AlignTop | Qt::AlignRight, kFeaturePanelMargin);
    connect(m_sketchPalette, &SketchPalette::edited, this, [this] {
        m_sketchPalette->apply(m_preferences);
        m_preferences.save();
        applyPreferences();
    });
    connect(m_sketchPalette, &SketchPalette::lookAtRequested, this, &MainWindow::lookAtSketch);
    connect(m_sketchPalette, &SketchPalette::gridSettingsRequested, this, &MainWindow::showGridSettings);

    overlay->pin(m_viewControls->createNavBar(nullptr), Qt::AlignBottom | Qt::AlignHCenter, kNavBarMargin);
    new ViewCubeArrows(m_view);
}

void MainWindow::updateActions()
{
    const bool sketching = m_sketchEditor->isActive();
    const bool inFeature = m_featurePanel->isActive();
    m_undo->setEnabled(m_session->canUndo());
    m_redo->setEnabled(m_session->canRedo());
    m_undo->setText(m_session->canUndo() ? tr("Undo %1").arg(m_session->undoLabel()) : tr("Undo"));
    m_redo->setText(m_session->canRedo() ? tr("Redo %1").arg(m_session->redoLabel()) : tr("Redo"));
    m_createSketch->setEnabled(!sketching && !inFeature);
    m_extrude->setEnabled(!inFeature);
    m_revolve->setEnabled(!inFeature);
    m_fillet->setEnabled(!sketching && !inFeature);
    m_chamfer->setEnabled(!sketching && !inFeature);
    for (QAction* action : {m_shell, m_pressPull, m_hole, m_move, m_appearance, m_pattern, m_offsetPlane,
                            m_planeAtAngle, m_midplane, m_axis, m_point}) {
        action->setEnabled(!sketching && !inFeature);
    }
    m_measure->setEnabled(!sketching && !inFeature);
    m_sketchTools->setEnabled(sketching);
    m_construction->setEnabled(sketching);
    m_delete->setEnabled(sketching);
    m_finishSketch->setEnabled(sketching);
    m_sketchBar->setVisible(sketching);
    m_paletteFrame->setVisible(sketching);
    m_viewControls->setLookAtEnabled(sketching);
    updateRepeatAction();
}

void MainWindow::updateTitle()
{
    QString title = m_session->displayName() + (m_session->isDirty() ? QStringLiteral("*") : QString());
    if (m_project) {
        title += QStringLiteral(" - ") + QString::fromStdString(m_project->name());
    }
    setWindowTitle(title + QStringLiteral(" - Kalve 137"));
    m_projectPanel->setCurrentDesign(m_session->path());
}

void MainWindow::updateStatus()
{
    if (m_sketchEditor->isActive()) {
        m_sketchStatus->setText(m_sketchEditor->solveSummary());
        m_selectionStatus->setText(m_sketchEditor->selectionSummary());
        statusBar()->showMessage(m_sketchEditor->hint());
        return;
    }
    m_sketchStatus->clear();
    if (!m_featurePanel->isActive()) {
        m_selectionStatus->setText(idleSelectionSummary());
    }
    if (m_pickingPlane) {
        statusBar()->showMessage(tr("Click a plane or a flat face to sketch on, Esc to cancel"));
        return;
    }
    statusBar()->showMessage(navigationHint(m_preferences.navigation.preset));
}

void MainWindow::refreshScene()
{
    if (m_featurePanel->isActive()) {
        m_featurePanel->refreshPreview();
        return;
    }
    const core::FeatureId sketch = m_sketchEditor->sketchId();
    m_scene->setEditingSketch(sketch);
    m_timeline->setEditing(sketch);
    const core::Document& document = m_session->document();
    const int index = sketch != 0 ? document.indexOf(sketch) : -1;
    if (index >= 0 && std::cmp_less(index + 1, document.marker())) {
        core::Document context = document;
        context.setMarker(static_cast<std::size_t>(index) + 1);
        m_scene->show(context, core::recompute(context), *m_session);
        return;
    }
    m_scene->show(document, m_session->model(), *m_session);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave()) {
        m_autosave->discard();
        if (!isMinimized()) {
            QSettings().setValue(kWindowGeometryKey, saveGeometry());
        }
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::showPreferences()
{
    PreferencesDialog dialog(m_preferences, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_preferences = dialog.preferences();
    m_preferences.save();
    applyPreferences();
}

void MainWindow::showGridSettings()
{
    GridSettingsDialog dialog(m_preferences, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_preferences = dialog.preferences();
    m_preferences.save();
    applyPreferences();
}

void MainWindow::lookAtSketch()
{
    if (m_session->document().find(m_sketchEditor->sketchId()) != nullptr) {
        const gp_Ax3 plane = m_session->sketchPlane(m_sketchEditor->sketchId());
        m_view->animateCamera([this, &plane] {
            m_view->lookAt(plane);
            fitView();
        });
    }
}

void MainWindow::applyPreferences()
{
    diagnostics::applyLevels(m_preferences.logLevels);
    m_view->setNavigation(m_preferences.navigation);
    const bool projectionChanged = m_view->isPerspective() != m_preferences.perspective;
    m_view->setPerspective(m_preferences.perspective);
    if (projectionChanged && m_view->isReady()) {
        fitView();
    }
    m_view->setGridVisible(m_preferences.showGrid);
    m_view->setGridSpacing(m_preferences.gridAdaptive, m_preferences.gridSpacing);
    if (m_sketchEditor->isActive()) {
        m_view->setGridLabels(m_preferences.sketchGridLabels);
    }
    m_scene->setVisualStyle(m_preferences.visualStyle);
    m_sketchEditor->setDisplay({m_preferences.sketchProfiles, m_preferences.sketchPoints,
                                m_preferences.sketchDimensions, m_preferences.constraintGlyphs,
                                m_preferences.sketchConstruction});
    m_sketchEditor->setGridSnap(m_preferences.sketchGridSnap);
    const bool incremental = m_preferences.incrementalMove;
    m_sketchEditor->setMoveIncrement(incremental ? m_preferences.moveIncrement : 0.0);
    m_featurePanel->setHandleIncrements(incremental ? m_preferences.moveIncrement : 0.0,
                                        incremental ? m_preferences.angleIncrement : 0.0);
    m_sketchPalette->load(m_preferences);
    m_viewControls->sync();
    updateStatus();
}

void MainWindow::showAbout()
{
    const QString platformName =
        QString::fromUtf8(platform::name().data(), static_cast<qsizetype>(platform::name().size()));
    QSvgRenderer wordmark(QStringLiteral(":/kalve137/kalve137-wordmark-small.svg"));
    const qreal scale = devicePixelRatioF();
    const QSizeF natural = wordmark.defaultSize();
    QPixmap pixmap(
        (QSizeF(natural.width() * kAboutWordmarkHeight / natural.height(), kAboutWordmarkHeight) * scale).toSize());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    wordmark.render(&painter);
    painter.end();
    pixmap.setDevicePixelRatio(scale);

    QMessageBox box(this);
    box.setWindowTitle(tr("About Kalve 137"));
    box.setIconPixmap(pixmap);
    box.setText(tr("Version %1 on %2<br><br>%3<br><br>Free software under the GNU GPL version 3 or later.")
                    .arg(QApplication::applicationVersion(), platformName,
                         m_view->glInfo().toHtmlEscaped().replace('\n', "<br>")));
    QPushButton* notices = box.addButton(tr("Third-Party Notices"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == notices) {
        showThirdPartyNotices(this);
    }
}

} // namespace kalve137::app
