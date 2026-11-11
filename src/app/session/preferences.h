// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/view/scene.h"

#include "viewer/navigation.h"

#include <QString>

namespace kalve137::app {

struct Preferences {
    bool reopenLastProject = true;

    viewer::Navigation navigation;

    VisualStyle visualStyle = VisualStyle::ShadedWithEdges;
    bool perspective = false;
    bool showGrid = true;

    bool lookAtSketch = true;
    bool sketchProfiles = true;
    bool sketchPoints = true;
    bool sketchDimensions = true;
    bool constraintGlyphs = true;
    bool sketchConstruction = true;
    bool sketchGridLabels = true;
    bool sketchGridSnap = true;

    bool gridAdaptive = true;
    double gridSpacing = 10.0;
    bool incrementalMove = false;
    double moveIncrement = 1.0;
    double angleIncrement = 15.0;

    QString logLevels;

    static Preferences load();
    void save() const;
};

QString displayName(viewer::NavigationPreset preset);
QString displayName(VisualStyle style);
QString navigationHint(viewer::NavigationPreset preset);

} // namespace kalve137::app
