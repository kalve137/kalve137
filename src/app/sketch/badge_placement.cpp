// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/badge_placement.h"

#include "app/sketch/sketch_editor_detail.h"

#include <map>

namespace kalve137::app {

using namespace sketch_editor_detail;

std::vector<BadgePlace> placeBadges(const sketch::Sketch& s)
{
    std::vector<BadgePlace> badges;
    std::map<Id, int> stacked;
    const auto near = [&](Id constraint, Id entity, const QString& icon) {
        Vec2 at;
        Vec2 side{0.0, 1.0};
        if (s.isCurve(entity)) {
            at = anchorOf(s, entity);
            const sketch::Curve* curve = s.curve(entity);
            if (curve->kind == CurveKind::Line) {
                const Vec2 d = normalized(s.point(curve->points[1])->pos - s.point(curve->points[0])->pos);
                side = {-d.y, d.x};
            } else {
                side = normalized(at - s.point(curve->points[0])->pos);
            }
        } else if (const sketch::Point* point = s.point(entity)) {
            at = point->pos;
            side = normalized(Vec2{1.0, 1.0});
        } else {
            return;
        }
        const int n = stacked[entity]++;
        badges.push_back({constraint, icon, at, side, kGlyphPixels + kGlyphStepPixels * n});
    };

    for (const auto& [id, constraint] : s.constraints()) {
        const QString icon = glyphFor(constraint.kind);
        if (icon.isEmpty()) {
            continue;
        }
        switch (constraint.kind) {
        case ConstraintKind::Horizontal:
        case ConstraintKind::Vertical:
            if (constraint.refs.size() == 1) {
                near(id, constraint.refs[0], icon);
            } else {
                const Vec2 a = s.point(constraint.refs[0])->pos;
                const Vec2 b = s.point(constraint.refs[1])->pos;
                badges.push_back({id, icon, (a + b) * 0.5, {}, 0.0});
            }
            break;
        case ConstraintKind::PointOnCurve:
        case ConstraintKind::Midpoint:
            near(id, constraint.refs[0], icon);
            break;
        case ConstraintKind::Symmetric:
            near(id, constraint.refs[0], icon);
            near(id, constraint.refs[1], icon);
            break;
        case ConstraintKind::Parallel:
        case ConstraintKind::Perpendicular:
        case ConstraintKind::Tangent:
        case ConstraintKind::Equal:
            for (Id ref : constraint.refs) {
                near(id, ref, icon);
            }
            break;
        case ConstraintKind::Coincident:
        case ConstraintKind::Distance:
        case ConstraintKind::HorizontalDistance:
        case ConstraintKind::VerticalDistance:
        case ConstraintKind::Radius:
        case ConstraintKind::Diameter:
        case ConstraintKind::Angle:
            break;
        }
    }
    for (const auto& [id, point] : s.points()) {
        if (point.fixed && id != sketch::kOriginId) {
            near(0, id, QStringLiteral("lock"));
        }
    }
    for (const auto& [id, curve] : s.curves()) {
        if (curve.fixed) {
            near(0, id, QStringLiteral("lock"));
        }
    }
    return badges;
}

} // namespace kalve137::app
