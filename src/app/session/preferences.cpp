// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/preferences.h"

#include <QCoreApplication>
#include <QSettings>

#include <array>

namespace kalve137::app {

namespace {

using viewer::NavigationPreset;

constexpr std::array kPresets = {NavigationPreset::MiddleOrbitsCtrlPans, NavigationPreset::MiddlePansShiftOrbits,
                                 NavigationPreset::MiddleOrbitsShiftPans};
constexpr std::array kStyles = {VisualStyle::Shaded, VisualStyle::ShadedWithEdges, VisualStyle::Wireframe,
                                VisualStyle::VisibleEdges};

const char* keyOf(NavigationPreset preset)
{
    switch (preset) {
    case NavigationPreset::MiddleOrbitsCtrlPans:
        return "orbit-ctrl-pan";
    case NavigationPreset::MiddlePansShiftOrbits:
        return "pan-shift-orbit";
    case NavigationPreset::MiddleOrbitsShiftPans:
        return "orbit-shift-pan";
    }
    return "orbit-ctrl-pan";
}

const char* keyOf(VisualStyle style)
{
    switch (style) {
    case VisualStyle::Shaded:
        return "shaded";
    case VisualStyle::ShadedWithEdges:
        return "shadedWithEdges";
    case VisualStyle::Wireframe:
        return "wireframe";
    case VisualStyle::VisibleEdges:
        return "visibleEdges";
    }
    return "shadedWithEdges";
}

template <typename Enum, std::size_t N>
Enum readEnum(const QSettings& settings, const char* key, const std::array<Enum, N>& values, Enum fallback)
{
    const QString stored = settings.value(key, QString::fromLatin1(keyOf(fallback))).toString();
    for (const Enum value : values) {
        if (stored == QLatin1String(keyOf(value))) {
            return value;
        }
    }
    return fallback;
}

double positive(const QSettings& settings, const char* key, double fallback)
{
    bool ok = false;
    const double value = settings.value(key, fallback).toDouble(&ok);
    return ok && value > 0.0 ? value : fallback;
}

QString tr(const char* text)
{
    return QCoreApplication::translate("Preferences", text);
}

} // namespace

Preferences Preferences::load()
{
    const Preferences defaults;
    Preferences p;
    QSettings settings;
    settings.beginGroup("preferences");
    p.reopenLastProject = settings.value("reopenLastProject", defaults.reopenLastProject).toBool();

    p.navigation.preset = readEnum(settings, "navigation/mouseScheme", kPresets, defaults.navigation.preset);
    p.navigation.reverseZoom = settings.value("navigation/reverseZoom", defaults.navigation.reverseZoom).toBool();
    p.navigation.zoomToCursor = settings.value("navigation/zoomToCursor", defaults.navigation.zoomToCursor).toBool();
    p.navigation.orbitAroundCursor =
        settings.value("navigation/orbitAroundCursor", defaults.navigation.orbitAroundCursor).toBool();
    p.navigation.constrainedOrbit =
        settings.value("navigation/constrainedOrbit", defaults.navigation.constrainedOrbit).toBool();
    p.navigation.animateTransitions =
        settings.value("navigation/animateTransitions", defaults.navigation.animateTransitions).toBool();

    p.visualStyle = readEnum(settings, "display/visualStyle", kStyles, defaults.visualStyle);
    p.perspective = settings.value("display/perspective", defaults.perspective).toBool();
    p.showGrid = settings.value("display/showGrid", defaults.showGrid).toBool();

    p.lookAtSketch = settings.value("sketch/lookAt", defaults.lookAtSketch).toBool();
    p.sketchProfiles = settings.value("sketch/profiles", defaults.sketchProfiles).toBool();
    p.sketchPoints = settings.value("sketch/points", defaults.sketchPoints).toBool();
    p.sketchDimensions = settings.value("sketch/dimensions", defaults.sketchDimensions).toBool();
    p.constraintGlyphs = settings.value("sketch/constraintGlyphs", defaults.constraintGlyphs).toBool();
    p.sketchConstruction = settings.value("sketch/construction", defaults.sketchConstruction).toBool();
    p.sketchGridLabels = settings.value("sketch/gridLabels", defaults.sketchGridLabels).toBool();
    p.sketchGridSnap = settings.value("sketch/gridSnap", defaults.sketchGridSnap).toBool();

    p.gridAdaptive = settings.value("grid/adaptive", defaults.gridAdaptive).toBool();
    p.gridSpacing = positive(settings, "grid/spacing", defaults.gridSpacing);
    p.incrementalMove = settings.value("grid/incrementalMove", defaults.incrementalMove).toBool();
    p.moveIncrement = positive(settings, "grid/moveIncrement", defaults.moveIncrement);
    p.angleIncrement = positive(settings, "grid/angleIncrement", defaults.angleIncrement);

    p.logLevels = settings.value("diagnostics/logLevels", defaults.logLevels).toString();
    return p;
}

void Preferences::save() const
{
    QSettings settings;
    settings.beginGroup("preferences");
    settings.setValue("reopenLastProject", reopenLastProject);

    settings.remove("navigation/preset");
    settings.setValue("navigation/mouseScheme", QString::fromLatin1(keyOf(navigation.preset)));
    settings.setValue("navigation/reverseZoom", navigation.reverseZoom);
    settings.setValue("navigation/zoomToCursor", navigation.zoomToCursor);
    settings.setValue("navigation/orbitAroundCursor", navigation.orbitAroundCursor);
    settings.setValue("navigation/constrainedOrbit", navigation.constrainedOrbit);
    settings.setValue("navigation/animateTransitions", navigation.animateTransitions);

    settings.setValue("display/visualStyle", QString::fromLatin1(keyOf(visualStyle)));
    settings.setValue("display/perspective", perspective);
    settings.setValue("display/showGrid", showGrid);

    settings.setValue("sketch/lookAt", lookAtSketch);
    settings.setValue("sketch/profiles", sketchProfiles);
    settings.setValue("sketch/points", sketchPoints);
    settings.setValue("sketch/dimensions", sketchDimensions);
    settings.setValue("sketch/constraintGlyphs", constraintGlyphs);
    settings.setValue("sketch/construction", sketchConstruction);
    settings.setValue("sketch/gridLabels", sketchGridLabels);
    settings.setValue("sketch/gridSnap", sketchGridSnap);

    settings.setValue("grid/adaptive", gridAdaptive);
    settings.setValue("grid/spacing", gridSpacing);
    settings.setValue("grid/incrementalMove", incrementalMove);
    settings.setValue("grid/moveIncrement", moveIncrement);
    settings.setValue("grid/angleIncrement", angleIncrement);

    settings.setValue("diagnostics/logLevels", logLevels);
}

QString displayName(NavigationPreset preset)
{
    switch (preset) {
    case NavigationPreset::MiddleOrbitsCtrlPans:
        return tr("Middle Orbits, Ctrl Pans");
    case NavigationPreset::MiddlePansShiftOrbits:
        return tr("Middle Pans, Shift Orbits");
    case NavigationPreset::MiddleOrbitsShiftPans:
        return tr("Middle Orbits, Shift Pans");
    }
    return {};
}

QString displayName(VisualStyle style)
{
    switch (style) {
    case VisualStyle::Shaded:
        return tr("Shaded");
    case VisualStyle::ShadedWithEdges:
        return tr("Shaded with Edges");
    case VisualStyle::Wireframe:
        return tr("Wireframe");
    case VisualStyle::VisibleEdges:
        return tr("Visible Edges Only");
    }
    return {};
}

QString navigationHint(NavigationPreset preset)
{
    switch (preset) {
    case NavigationPreset::MiddleOrbitsCtrlPans:
        return tr("Orbit: middle drag   Pan: Ctrl + middle drag   Zoom: wheel or Shift + middle drag   Fit: F6");
    case NavigationPreset::MiddlePansShiftOrbits:
        return tr("Orbit: Shift + middle drag   Pan: middle drag   Zoom: wheel or Ctrl + middle drag   Fit: F6");
    case NavigationPreset::MiddleOrbitsShiftPans:
        return tr("Orbit: middle drag   Pan: Shift + middle drag   Zoom: wheel or Ctrl + middle drag   Fit: F6");
    }
    return {};
}

} // namespace kalve137::app
