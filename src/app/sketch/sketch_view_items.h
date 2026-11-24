// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/annotations.h"
#include "render/draw_list.h"
#include "sketch/sketch.h"
#include "sketch/snapshot.h"
#include "snapshot/color.h"
#include "snapshot/mesh.h"

#include <Aspect_TypeOfMarker.hxx>

#include <QColor>
#include <QString>

#include <optional>

namespace kalve137::app {

snapshot::Color colorOf(const QColor& color, double transparency = 0.0);
snapshot::Polyline arcPolyline(const sketch::Placement& placement, sketch::Vec2 center, double radius, double from,
                               double sweep);
render::MarkerShape markerShapeOf(Aspect_TypeOfMarker type);
render::MarkerScale markerScaleOf(double scale);
std::optional<render::BadgeIcon> badgeIconOf(const QString& name);

} // namespace kalve137::app
