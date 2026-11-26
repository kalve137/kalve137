// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/main_window.h"

#include "app/diagnostics/diagnostics.h"
#include "app/feature/feature_panel.h"
#include "app/feature/measure_panel.h"
#include "app/panels/browser_panel.h"
#include "app/panels/parameters_dialog.h"
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
#include "app/shell/toolbar_fit.h"
#include "app/sketch/sketch_palette.h"
#include "app/theme/fonts.h"
#include "app/theme/icons.h"
#include "app/theme/theme.h"
#include "app/view/body_opacity.h"
#include "app/view/camera_commands.h"
#include "app/view/grid_settings.h"
#include "app/view/scene.h"
#include "app/view/sketch_snapshots.h"
#include "app/view/view_controls.h"
#include "app/view/view_cube_arrows.h"
#include "app/view/view_overlay.h"
#include "app/view/view_projection.h"
#include "app/view/viewport_picker.h"
#include "core/naming.h"
#include "core/recompute.h"

#include "platform/platform.h"
#include "render/rhi/viewport.h"
#include "viewer/occt_view_widget.h"

#include <OcctQtTools.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
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

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace kalve137::app {

namespace {

const char* const kWindowGeometryKey = "windowGeometry";

} // namespace

MainWindow::MainWindow(const StartupOptions& options, QWidget* parent) : QMainWindow(parent)
{
    m_session = new DesignSession(this);
    m_autosave = new Autosave(*m_session, this);

    if (options.view == ViewKind::New) {
        m_hiddenView = std::make_unique<viewer::OcctViewWidget>();
        m_view = m_hiddenView.get();
        const render::GraphicsApi api = options.graphicsApi.value_or(render::platformGraphicsApi());
        if (const render::StartProblem problem = render::checkStart(api); problem == render::StartProblem::None) {
            m_viewport = new render::Viewport(api, this);
            m_viewport->setTextFamily(fonts::families().qt);
            setCentralWidget(m_viewport);
        } else {
            setCentralWidget(render::makeFailureNotice(api, problem, this));
        }
    } else {
        m_view = new viewer::OcctViewWidget(this);
        setCentralWidget(m_view);
    }
    applyViewportTheme();

    m_scene = std::make_unique<Scene>(m_view, m_session->snapshots(), m_session->rays());
    m_picks = m_scene.get();
    if (m_viewport != nullptr) {
        m_viewportPicker = new ViewportPicker(*m_viewport, *m_session, *m_scene, m_session->rays(), this);
        m_scene->setPicks(*m_viewportPicker);
        m_scene->setDrawsBodies(false);
        m_picks = m_viewportPicker;
        connect(m_scene.get(), &Scene::rebuilt, this, [this] {
            m_viewport->setDatums(m_scene->datums());
            m_viewport->setSketchLook(sketchLook(*m_scene));
        });
        connect(m_scene.get(), &Scene::pickModeChanged, this, &MainWindow::showPickModeOpacity);
    }
    m_viewProjection = new ViewProjection(*m_view, m_viewport, this);
    m_sketchEditor = new SketchEditor(*m_session, *m_scene, *m_viewProjection, this);
    m_cameraCommands = new CameraCommands(*m_view, m_viewport, [this] { return sceneIsEmpty(); }, this);

    createActions();
    createDocks();
    createOverlays();
    createMenus();
    createToolBars();

    m_selectionStatus = new QLabel(this);
    statusBar()->addPermanentWidget(m_selectionStatus);
    m_sketchStatus = new QLabel(this);
    statusBar()->addPermanentWidget(m_sketchStatus);
    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("statusHint"));
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(m_hint, 1);
    new ModalScrim(this);

    if (!restoreGeometry(QSettings().value(kWindowGeometryKey).toByteArray()) ||
        QGuiApplication::screenAt(geometry().center()) == nullptr) {
        resize(theme::onScreen(*this, sizeHint()));
        move(QGuiApplication::primaryScreen()->availableGeometry().center() - rect().center());
    }

    connect(m_session, &DesignSession::changed, this, [this] {
        m_featurePanel->cancel();
        refreshScene();
        updateActions();
    });
    connect(m_session, &DesignSession::visibilityChanged, this, &MainWindow::refreshScene);
    connect(m_session, &DesignSession::bodiesVisibilityChanged, this, &MainWindow::refreshVisibility);
    connect(m_session, &DesignSession::fileStateChanged, this, &MainWindow::updateTitle);
    if (m_viewport != nullptr) {
        connect(m_viewport, &render::Viewport::ready, this, [this] {
            refreshScene();
            m_cameraCommands->fit(CameraCommands::Move::Jump);
        });
        connect(m_view, &viewer::OcctViewWidget::gridChanged, this, &MainWindow::syncViewportGrid);
    } else {
        connect(m_view, &viewer::OcctViewWidget::ready, this, [this] {
            diagnostics::setGlInfo(m_view->glInfo());
            refreshScene();
            m_cameraCommands->fit(CameraCommands::Move::Jump);
        });
    }
    connect(m_cameraCommands, &CameraCommands::menuRequested, this, &MainWindow::showMarkingMenu);
    connect(m_picks, &PickSource::selectionChanged, this, &MainWindow::onViewSelection);
    connect(m_picks, &PickSource::hoverChanged, this, &MainWindow::onViewHover);
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
        updatePickMode();
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
            updatePickMode();
        }
        updateActions();
    });
    connect(m_measurePanel, &MeasurePanel::activeChanged, this, [this](bool active) {
        m_measureFrame->setVisible(active);
        if (!active) {
            updatePickMode();
        }
        updateActions();
    });
    connect(m_featurePanel, &FeaturePanel::message, this,
            [this](const QString& text) { statusBar()->showMessage(text, kMessageTimeoutMs); });
    connect(m_featurePanel, &FeaturePanel::selectionSummary, m_selectionStatus, &QLabel::setText);
    connect(m_session, &DesignSession::editRefused, this,
            [this](const QString& text) { statusBar()->showMessage(text, kMessageTimeoutMs); });
    connect(theme::notifier(), &theme::Notifier::changed, this, [this] {
        applyViewportTheme();
        if (m_view->isReady()) {
            m_scene->restyle();
        }
    });

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

void MainWindow::showViewportScene(snapshot::Scene scene)
{
    m_viewport->setScene(withBodyOpacity(std::move(scene), m_scene->pickMode()));
}

void MainWindow::showPickModeOpacity()
{
    snapshot::Scene shown = withBodyOpacity(m_viewport->scene(), m_scene->pickMode());
    const snapshot::Scene& current = m_viewport->scene();
    if (shown.bodies != current.bodies || shown.hidden != current.hidden) {
        m_viewport->setScene(std::move(shown));
    }
}

} // namespace kalve137::app
