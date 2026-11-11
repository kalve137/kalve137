// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_palette.h"

#include "app/theme/theme.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace kalve137::app {

SketchPalette::SketchPalette(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(theme::kPanelMargins);
    auto* title = new QLabel(tr("Sketch Palette"), this);
    title->setObjectName("panelTitle");
    layout->addWidget(title);

    m_profiles = addToggle(tr("Profiles"));
    m_points = addToggle(tr("Points"));
    m_dimensions = addToggle(tr("Dimensions"));
    m_constraints = addToggle(tr("Constraints"));
    m_construction = addToggle(tr("Construction"));
    m_showGrid = addToggle(tr("Show Grid"));
    m_snap = addToggle(tr("Snap to Grid"));

    auto* lookAt = new QPushButton(tr("Look At"), this);
    lookAt->setToolTip(tr("Turn the view to face the sketch plane"));
    auto* grid = new QPushButton(tr("Grid Settings..."), this);
    layout->addWidget(lookAt);
    layout->addWidget(grid);
    connect(lookAt, &QPushButton::clicked, this, &SketchPalette::lookAtRequested);
    connect(grid, &QPushButton::clicked, this, &SketchPalette::gridSettingsRequested);
}

QCheckBox* SketchPalette::addToggle(const QString& text)
{
    auto* check = new QCheckBox(text, this);
    layout()->addWidget(check);
    connect(check, &QCheckBox::toggled, this, &SketchPalette::edited);
    return check;
}

void SketchPalette::load(const Preferences& preferences)
{
    const std::pair<QCheckBox*, bool> toggles[] = {
        {m_profiles, preferences.sketchProfiles},
        {m_points, preferences.sketchPoints},
        {m_dimensions, preferences.sketchDimensions},
        {m_constraints, preferences.constraintGlyphs},
        {m_construction, preferences.sketchConstruction},
        {m_showGrid, preferences.showGrid},
        {m_snap, preferences.sketchGridSnap},
    };
    for (const auto& [check, on] : toggles) {
        const QSignalBlocker blocker(check);
        check->setChecked(on);
    }
}

void SketchPalette::apply(Preferences& preferences) const
{
    preferences.sketchProfiles = m_profiles->isChecked();
    preferences.sketchPoints = m_points->isChecked();
    preferences.sketchDimensions = m_dimensions->isChecked();
    preferences.constraintGlyphs = m_constraints->isChecked();
    preferences.sketchConstruction = m_construction->isChecked();
    preferences.showGrid = m_showGrid->isChecked();
    preferences.sketchGridSnap = m_snap->isChecked();
}

} // namespace kalve137::app
