// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_view_items.h"

#include "snapshot/chord.h"
#include "viewer/style.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace kalve137::app {

namespace {

constexpr std::array<std::pair<const char*, render::BadgeIcon>, render::kBadgeIcons.size()> kIconNames{{
    {"coincident", render::BadgeIcon::Coincident},
    {"equal", render::BadgeIcon::Equal},
    {"horizontal", render::BadgeIcon::Horizontal},
    {"lock", render::BadgeIcon::Lock},
    {"midpoint", render::BadgeIcon::Midpoint},
    {"parallel", render::BadgeIcon::Parallel},
    {"perpendicular", render::BadgeIcon::Perpendicular},
    {"symmetric", render::BadgeIcon::Symmetric},
    {"tangent", render::BadgeIcon::Tangent},
    {"vertical", render::BadgeIcon::Vertical},
}};

} // namespace

snapshot::Color colorOf(const QColor& color, double transparency)
{
    return {static_cast<float>(color.redF()), static_cast<float>(color.greenF()), static_cast<float>(color.blueF()),
            static_cast<float>(color.alphaF() * (1.0 - transparency))};
}

snapshot::Polyline arcPolyline(const sketch::Placement& placement, sketch::Vec2 center, double radius, double from,
                               double sweep)
{
    const int segments = snapshot::arcSegments(radius, sweep, snapshot::chordDeflection(2.0 * radius));
    snapshot::Polyline points;
    points.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const double angle = from + (sweep * i / segments);
        points.push_back(
            sketch::placed(placement, {center.x + (radius * std::cos(angle)), center.y + (radius * std::sin(angle))}));
    }
    return points;
}

render::MarkerShape markerShapeOf(Aspect_TypeOfMarker type)
{
    if (type == Aspect_TOM_O_POINT) {
        return render::MarkerShape::Ring;
    }
    if (type == Aspect_TOM_PLUS) {
        return render::MarkerShape::Plus;
    }
    if (type == Aspect_TOM_O_PLUS) {
        return render::MarkerShape::RingPlus;
    }
    if (type == Aspect_TOM_O_STAR) {
        return render::MarkerShape::Star;
    }
    return render::MarkerShape::Ball;
}

render::MarkerScale markerScaleOf(double scale)
{
    if (scale >= viewer::style::kMarkerLarge) {
        return render::MarkerScale::Large;
    }
    return scale >= viewer::style::kMarkerMedium ? render::MarkerScale::Medium : render::MarkerScale::Small;
}

std::optional<render::BadgeIcon> badgeIconOf(const QString& name)
{
    for (const auto& [text, icon] : kIconNames) {
        if (name == QLatin1String(text)) {
            return icon;
        }
    }
    return std::nullopt;
}

} // namespace kalve137::app
