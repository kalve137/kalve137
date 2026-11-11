// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/view_controls.h"

#include "app/theme/icons.h"
#include "app/theme/theme.h"

#include <QAction>
#include <QActionGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenu>
#include <QToolButton>

#include <utility>

namespace kalve137::app {

namespace {

constexpr int kNavIconSize = 24;
constexpr int kNavSpacing = theme::kSpaceXs;

constexpr VisualStyle kStyles[] = {VisualStyle::Shaded, VisualStyle::ShadedWithEdges, VisualStyle::Wireframe,
                                   VisualStyle::VisibleEdges};

void setShortcutTip(QAction* action, const QKeySequence& key)
{
    action->setShortcut(key);
    action->setToolTip(
        QStringLiteral("%1 (%2)").arg(action->text().remove('&'), key.toString(QKeySequence::NativeText)));
}

} // namespace

ViewControls::ViewControls(Preferences& preferences, QAction* showOrigin, QWidget* parent)
    : QObject(parent), m_preferences(preferences), m_showOrigin(showOrigin)
{
    m_home = new QAction(icons::tinted("home"), tr("&Home"), parent);
    setShortcutTip(m_home, QKeySequence(Qt::Key_Home));
    connect(m_home, &QAction::triggered, this, &ViewControls::homeRequested);

    using viewer::StandardView;
    const std::pair<StandardView, QString> views[] = {
        {StandardView::Front, tr("&Front")}, {StandardView::Back, tr("&Back")}, {StandardView::Left, tr("&Left")},
        {StandardView::Right, tr("&Right")}, {StandardView::Top, tr("&Top")},   {StandardView::Bottom, tr("B&ottom")},
    };
    for (const auto& [view, text] : views) {
        auto* action = new QAction(text, parent);
        connect(action, &QAction::triggered, this, [this, view] { emit standardViewRequested(view); });
        m_standardViews.push_back(action);
    }

    m_fit = new QAction(icons::tinted("fit_all"), tr("&Fit All"), parent);
    setShortcutTip(m_fit, QKeySequence(Qt::Key_F6));
    connect(m_fit, &QAction::triggered, this, &ViewControls::fitRequested);

    m_lookAt = new QAction(icons::tinted("look_at"), tr("&Look At Sketch"), parent);
    connect(m_lookAt, &QAction::triggered, this, &ViewControls::lookAtRequested);

    m_constrainedOrbit = new QAction(icons::tinted("constrained_orbit"), tr("&Constrained Orbit"), parent);
    m_constrainedOrbit->setCheckable(true);
    m_constrainedOrbit->setToolTip(tr("Constrained orbit: keep Z up while orbiting"));
    connect(m_constrainedOrbit, &QAction::toggled, this, [this](bool on) {
        m_preferences.navigation.constrainedOrbit = on;
        emit preferencesEdited();
    });

    m_showGrid = new QAction(icons::tinted("show_grid"), tr("Show &Grid"), parent);
    m_showGrid->setCheckable(true);
    connect(m_showGrid, &QAction::toggled, this, [this](bool on) {
        m_preferences.showGrid = on;
        emit preferencesEdited();
    });

    m_snapToGrid = new QAction(icons::tinted("snap_grid"), tr("S&nap to Grid"), parent);
    m_snapToGrid->setCheckable(true);
    m_snapToGrid->setToolTip(tr("Snap sketch points to the visible grid"));
    connect(m_snapToGrid, &QAction::toggled, this, [this](bool on) {
        m_preferences.sketchGridSnap = on;
        emit preferencesEdited();
    });

    m_gridSettings = new QAction(tr("Grid Se&ttings..."), parent);
    connect(m_gridSettings, &QAction::triggered, this, &ViewControls::gridSettingsRequested);

    m_showOrigin->setIcon(icons::tinted("origin"));

    m_styles = new QActionGroup(this);
    int number = 1;
    for (const VisualStyle style : kStyles) {
        auto* action = new QAction(displayName(style), m_styles);
        action->setCheckable(true);
        action->setData(static_cast<int>(style));
        action->setShortcut(QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_0 + number++)));
        parent->addAction(action);
        connect(action, &QAction::triggered, this, [this, style] {
            m_preferences.visualStyle = style;
            emit preferencesEdited();
        });
    }

    m_cameras = new QActionGroup(this);
    for (const bool perspective : {false, true}) {
        auto* action = new QAction(perspective ? tr("Perspective") : tr("Orthographic"), m_cameras);
        action->setCheckable(true);
        action->setData(perspective);
        connect(action, &QAction::triggered, this, [this, perspective] {
            m_preferences.perspective = perspective;
            emit preferencesEdited();
        });
    }

    sync();
}

void ViewControls::addToMenu(QMenu* menu) const
{
    menu->addAction(m_home);
    auto* views = menu->addMenu(tr("Standard &Views"));
    for (QAction* action : m_standardViews) {
        views->addAction(action);
    }
    menu->addAction(m_fit);
    menu->addAction(m_lookAt);
    menu->addAction(m_constrainedOrbit);
    menu->addSeparator();
    menu->addMenu(createStyleMenu(menu));
    menu->addMenu(createCameraMenu(menu));
    menu->addAction(m_showGrid);
    menu->addAction(m_snapToGrid);
    menu->addAction(m_gridSettings);
    menu->addAction(m_showOrigin);
}

QFrame* ViewControls::createNavBar(QWidget* parent) const
{
    auto* bar = new QFrame(parent);
    bar->setObjectName("navBar");
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(theme::kSpaceM, theme::kSpaceS, theme::kSpaceM, theme::kSpaceS);
    layout->setSpacing(kNavSpacing);

    const auto addButton = [bar, layout](QAction* action) {
        auto* button = new QToolButton(bar);
        button->setDefaultAction(action);
        button->setIconSize(QSize(kNavIconSize, kNavIconSize));
        button->setAutoRaise(true);
        layout->addWidget(button);
        return button;
    };
    const auto addMenuButton = [bar, layout](const QIcon& icon, const QString& tip, QMenu* menu) {
        auto* button = new QToolButton(bar);
        button->setIcon(icon);
        button->setToolTip(tip);
        button->setIconSize(QSize(kNavIconSize, kNavIconSize));
        button->setAutoRaise(true);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);
        layout->addWidget(button);
    };
    const auto addSeparator = [bar, layout] {
        auto* line = new QFrame(bar);
        line->setObjectName("navSeparator");
        line->setFixedWidth(1);
        layout->addWidget(line);
    };

    addButton(m_home);
    addButton(m_fit);
    addButton(m_lookAt);
    addButton(m_constrainedOrbit);
    addSeparator();
    addMenuButton(icons::tinted("visual_style"), tr("Visual Style"), createStyleMenu(bar));
    addMenuButton(icons::tinted("camera"), tr("Camera"), createCameraMenu(bar));
    addSeparator();
    QToolButton* grid = addButton(m_showGrid);
    auto* gridMenu = new QMenu(grid);
    gridMenu->addAction(m_showGrid);
    gridMenu->addAction(m_snapToGrid);
    gridMenu->addSeparator();
    gridMenu->addAction(m_gridSettings);
    grid->setMenu(gridMenu);
    grid->setPopupMode(QToolButton::MenuButtonPopup);
    addButton(m_showOrigin);
    return bar;
}

void ViewControls::sync()
{
    const QSignalBlocker orbitBlocker(m_constrainedOrbit);
    m_constrainedOrbit->setChecked(m_preferences.navigation.constrainedOrbit);
    const QSignalBlocker gridBlocker(m_showGrid);
    m_showGrid->setChecked(m_preferences.showGrid);
    const QSignalBlocker snapBlocker(m_snapToGrid);
    m_snapToGrid->setChecked(m_preferences.sketchGridSnap);
    for (QAction* action : m_styles->actions()) {
        action->setChecked(action->data().toInt() == static_cast<int>(m_preferences.visualStyle));
    }
    for (QAction* action : m_cameras->actions()) {
        action->setChecked(action->data().toBool() == m_preferences.perspective);
    }
}

void ViewControls::setLookAtEnabled(bool enabled)
{
    m_lookAt->setEnabled(enabled);
}

QMenu* ViewControls::createStyleMenu(QWidget* parent) const
{
    auto* menu = new QMenu(tr("Visual &Style"), parent);
    menu->addActions(m_styles->actions());
    return menu;
}

QMenu* ViewControls::createCameraMenu(QWidget* parent) const
{
    auto* menu = new QMenu(tr("&Camera"), parent);
    menu->addActions(m_cameras->actions());
    return menu;
}

} // namespace kalve137::app
