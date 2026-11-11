// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/sketch/constraint_badge.h"

#include "core/expression.h"
#include "sketch/sketch.h"
#include "sketch/solver.h"
#include "viewer/style.h"

#include <OcctQtTools.h>

#include <Quantity_Color.hxx>
#include <TCollection_ExtendedString.hxx>

#include <QColor>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>

namespace kalve137::app::sketch_editor_detail {

using sketch::ConstraintKind;
using sketch::CurveKind;
using sketch::Id;
using sketch::Vec2;

inline constexpr double kPickPixels = 9.0;
inline constexpr double kInferencePixels = 6.0;
inline constexpr double kInferenceLineWidth = viewer::style::kStrokeHairline;
inline constexpr double kMinCrossing = 0.25;
inline constexpr double kAutoAxisDegrees = 3.0;
inline constexpr double kGlyphPixels = 18.0;
inline constexpr double kInputOffsetPixels = 24.0;
inline constexpr double kInputGapPixels = 12.0;
inline constexpr double kAngleArcPixels = 30.0;
inline constexpr double kCursorMarkerScale = viewer::style::kMarkerSmall;
inline constexpr double kSnapMarkerScale = viewer::style::kMarkerMedium;
inline constexpr double kMidpointMarkerScale = viewer::style::kMarkerLarge;
inline constexpr double kGlyphStepPixels = kConstraintBadgePixels + 3.0;
inline constexpr double kDimensionTextPixels = 20.0;
inline constexpr double kDimensionFlyoutPixels = 28.0;
inline constexpr double kAngleDimensionPixels = 64.0;
inline constexpr double kRescaleTolerance = 0.02;
inline constexpr int kRescaleDelayMs = 120;
inline constexpr double kProfileTransparency = viewer::style::kTintFaint;
inline constexpr double kTwoPi = 2.0 * std::numbers::pi;

inline double dot(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}
inline double length(Vec2 a)
{
    return std::hypot(a.x, a.y);
}

inline Vec2 normalized(Vec2 a)
{
    const double l = length(a);
    return l > 0.0 ? a * (1.0 / l) : Vec2{1.0, 0.0};
}

inline Vec2 closestOnSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 ab = b - a;
    const double l2 = dot(ab, ab);
    if (l2 <= 0.0) {
        return a;
    }
    const double t = std::clamp(dot(p - a, ab) / l2, 0.0, 1.0);
    return a + ab * t;
}

inline double wrapAngle(double angle)
{
    angle = std::fmod(angle, kTwoPi);
    return angle < 0.0 ? angle + kTwoPi : angle;
}

inline double angleOf(Vec2 v)
{
    return std::atan2(v.y, v.x);
}

inline bool angleInArc(double angle, double start, double end)
{
    return wrapAngle(angle - start) <= wrapAngle(end - start);
}

inline QString formatValue(double value)
{
    QString text = QString::number(value, 'f', 3);
    while (text.endsWith('0')) {
        text.chop(1);
    }
    if (text.endsWith('.')) {
        text.chop(1);
    }
    return text;
}

inline core::Quantity quantityOf(ConstraintKind kind)
{
    return kind == ConstraintKind::Angle ? core::Quantity::Angle : core::Quantity::Length;
}

inline Quantity_Color occt(const QColor& color)
{
    return OcctQtTools::qtColorToOcct(color);
}

inline bool isRound(CurveKind kind)
{
    switch (kind) {
    case CurveKind::Line:
        return false;
    case CurveKind::Circle:
    case CurveKind::Arc:
        return true;
    }
    return false;
}

inline QString glyphFor(ConstraintKind kind)
{
    switch (kind) {
    case ConstraintKind::Horizontal:
        return QStringLiteral("horizontal");
    case ConstraintKind::Vertical:
        return QStringLiteral("vertical");
    case ConstraintKind::Parallel:
        return QStringLiteral("parallel");
    case ConstraintKind::Perpendicular:
        return QStringLiteral("perpendicular");
    case ConstraintKind::Tangent:
        return QStringLiteral("tangent");
    case ConstraintKind::Equal:
        return QStringLiteral("equal");
    case ConstraintKind::PointOnCurve:
        return QStringLiteral("coincident");
    case ConstraintKind::Midpoint:
        return QStringLiteral("midpoint");
    case ConstraintKind::Symmetric:
        return QStringLiteral("symmetric");
    case ConstraintKind::Coincident:
    case ConstraintKind::Distance:
    case ConstraintKind::HorizontalDistance:
    case ConstraintKind::VerticalDistance:
    case ConstraintKind::Radius:
    case ConstraintKind::Diameter:
    case ConstraintKind::Angle:
        return {};
    }
    return {};
}

inline Vec2 anchorOf(const sketch::Sketch& sketch, Id curveId)
{
    const sketch::Curve* curve = sketch.curve(curveId);
    switch (curve->kind) {
    case CurveKind::Line:
        return *sketch.midpoint(curveId);
    case CurveKind::Circle: {
        const Vec2 c = sketch.point(curve->points[0])->pos;
        const double a = std::numbers::pi / 4.0;
        return c + Vec2{std::cos(a), std::sin(a)} * curve->radius;
    }
    case CurveKind::Arc: {
        const Vec2 c = sketch.point(curve->points[0])->pos;
        const double start = angleOf(sketch.point(curve->points[1])->pos - c);
        const double end = angleOf(sketch.point(curve->points[2])->pos - c);
        const double mid = start + wrapAngle(end - start) / 2.0;
        return c + Vec2{std::cos(mid), std::sin(mid)} * curve->radius;
    }
    }
    return {};
}

inline sketch::Sketch withDimensionsIfSolvable(const std::function<sketch::Sketch(bool)>& build,
                                               std::size_t redundantBefore)
{
    sketch::Sketch candidate = build(true);
    sketch::Sketch probe = candidate;
    const sketch::SolveResult result = sketch::solve(probe);
    if (!result.solved || !result.conflicting.empty() || result.redundant.size() > redundantBefore) {
        return build(false);
    }
    return candidate;
}

} // namespace kalve137::app::sketch_editor_detail
