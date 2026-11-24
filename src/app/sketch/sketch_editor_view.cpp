// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/sketch/badge_placement.h"
#include "app/sketch/sketch_editor_detail.h"
#include "app/sketch/sketch_view_items.h"
#include "app/theme/theme.h"

#include "render/rhi/rhi_view.h"
#include "render/style.h"
#include "sketch/dimension.h"
#include "sketch/dimension_layout.h"
#include "sketch/profiles.h"
#include "sketch/snapshot.h"
#include "viewer/style.h"

#include <TopoDS_Face.hxx>
#include <gp_XYZ.hxx>

#include <QColor>
#include <QString>

#include <cmath>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace kalve137::app {

namespace style = viewer::style;

using namespace sketch_editor_detail;

namespace {

constexpr int kBadgeFillAlpha = 235;

camera::Vec3 vecOf(const snapshot::Point& point)
{
    return {point[0], point[1], point[2]};
}

bool sameXyz(const gp_XYZ& a, const gp_XYZ& b)
{
    return a.X() == b.X() && a.Y() == b.Y() && a.Z() == b.Z();
}

bool sameAxes(const gp_Ax3& a, const gp_Ax3& b)
{
    return sameXyz(a.Location().XYZ(), b.Location().XYZ()) && sameXyz(a.Direction().XYZ(), b.Direction().XYZ()) &&
           sameXyz(a.XDirection().XYZ(), b.XDirection().XYZ());
}

} // namespace

SketchEditor::DrawCache& SketchEditor::drawCache(const sketch::Sketch& shown)
{
    if (m_drawCache && m_drawCache->feature == m_sketchId && m_drawCache->sketch == shown) {
        return *m_drawCache;
    }
    sketch::Sketch probe = shown;
    sketch::SolveResult solve = sketch::solve(probe);
    std::set<Id> conflicted;
    for (Id constraint : solve.conflicting) {
        if (const sketch::Constraint* c = shown.constraint(constraint)) {
            conflicted.insert(c->refs.begin(), c->refs.end());
        }
    }
    return m_drawCache.emplace(DrawCache{.sketch = shown,
                                         .feature = m_sketchId,
                                         .solve = std::move(solve),
                                         .conflicted = std::move(conflicted),
                                         .snapshot = std::nullopt,
                                         .plane = {},
                                         .profiles = false});
}

const SketchEditor::DrawCache* SketchEditor::shownCache(const sketch::Sketch& shown) const
{
    if (m_drawCache && m_drawCache->snapshot && m_drawCache->feature == m_sketchId && m_drawCache->sketch == shown) {
        return &*m_drawCache;
    }
    return nullptr;
}

void SketchEditor::redrawHover()
{
    const sketch::Sketch& shown = m_dragPreview ? *m_dragPreview : current();
    const DrawCache* cache = shownCache(shown);
    if (m_newView == nullptr || m_pending || cache == nullptr) {
        redraw();
        return;
    }
    drawViewSketch(shown, cache->conflicted);
    publish();
}

void SketchEditor::relayoutView()
{
    const sketch::Sketch& shown = m_dragPreview ? *m_dragPreview : current();
    const DrawCache* cache = shownCache(shown);
    if (m_newView == nullptr || m_pending || cache == nullptr) {
        redraw();
        return;
    }
    drawViewConstraints(shown, cache->solve);
    publish();
}

const snapshot::Sketch& SketchEditor::cachedSnapshot(const sketch::Sketch& shown)
{
    DrawCache& cache = drawCache(shown);
    const gp_Ax3 ax = plane();
    if (!cache.snapshot || !sameAxes(cache.plane, ax) || cache.profiles != m_display.profiles) {
        const std::vector<TopoDS_Face> faces =
            m_display.profiles ? sketch::findProfiles(shown, ax) : std::vector<TopoDS_Face>{};
        cache.snapshot = sketch::snapshotOf(shown, ax, faces, static_cast<int>(m_sketchId));
        cache.plane = ax;
        cache.profiles = m_display.profiles;
    }
    return *cache.snapshot;
}

void SketchEditor::drawViewSketch(const sketch::Sketch& shown, const std::set<Id>& conflicted)
{
    const theme::Palette& colors = theme::palette();
    const snapshot::Sketch& snapshot = cachedSnapshot(shown);
    m_viewItems = {};
    m_viewItems.profiles = snapshot.profiles;
    const snapshot::Color halo = colorOf(colors.viewport.halo);
    const std::set<Id> lost = lostProjections();
    for (const snapshot::SketchCurve& curve : snapshot.curves) {
        const Id id = curve.id;
        if (curve.construction && !m_display.construction && !m_selection.contains(id)) {
            continue;
        }
        QColor color =
            m_solve.fullyConstrained.contains(id) ? colors.viewport.sketchConstrained : colors.viewport.sketchFree;
        if (curve.construction) {
            color = colors.viewport.sketchConstruction;
        }
        if (const sketch::Curve* source = shown.curve(id); source != nullptr && source->projected) {
            color = colors.viewport.sketchProjected;
        }
        if (conflicted.contains(id) || lost.contains(id)) {
            color = colors.chrome.conflict;
        }
        if (m_selection.contains(id)) {
            color = colors.chrome.accent;
        } else if (m_hover == id) {
            color = colors.chrome.accent.lighter(130);
        }
        const bool marked = m_selection.contains(id) || m_hover == id;
        m_viewItems.halos.push_back(
            {.points = curve.points, .color = halo, .width = style::kStrokeHalo, .pattern = render::kSolidPattern});
        m_viewItems.lines.push_back({.points = curve.points,
                                     .color = colorOf(color),
                                     .width = marked ? style::kStrokeBold : style::kStrokeRegular,
                                     .pattern = curve.construction ? render::kDashPattern : render::kSolidPattern});
    }
    for (const snapshot::SketchPoint& point : snapshot.points) {
        const Id id = point.id;
        if (!m_display.points && !m_selection.contains(id) && m_hover != id) {
            continue;
        }
        const bool fixed = (point.flags & snapshot::kSketchPointFixed) != 0;
        QColor color = m_solve.fullyConstrained.contains(id) || fixed ? colors.viewport.sketchConstrained
                                                                      : colors.viewport.sketchFree;
        if ((point.flags & snapshot::kSketchPointOrigin) != 0) {
            color = colors.viewport.annotation;
        }
        if (const sketch::Point* source = shown.point(id); source != nullptr && source->projected) {
            color = lost.contains(id) ? colors.chrome.conflict : colors.viewport.sketchProjected;
        }
        if (m_selection.contains(id) || m_hover == id) {
            color = colors.chrome.accent;
        }
        m_viewItems.markers.push_back(
            {.anchor = vecOf(point.point),
             .shape = render::MarkerShape::Ball,
             .scale = m_hover == id ? render::MarkerScale::Medium : render::MarkerScale::Small,
             .color = colorOf(color),
             .flags = render::kMarkerTopmost,
             .pick = 0});
    }
}

void SketchEditor::drawViewConstraints(const sketch::Sketch& s, const sketch::SolveResult& solve)
{
    const theme::Palette& colors = theme::palette();
    const sketch::Placement placement = sketch::placementOf(plane());
    const double wpp = worldPerPixel();
    m_drawnScale = wpp;
    m_editedLabel.reset();
    m_viewConstraints = {};
    if (!(wpp > 0.0)) {
        return;
    }
    const std::set<Id> conflicting(solve.conflicting.begin(), solve.conflicting.end());
    const auto at = [&](Vec2 p) { return sketch::placed(placement, p); };

    if (m_display.constraints) {
        QColor fill = colors.chrome.surface;
        fill.setAlpha(kBadgeFillAlpha);
        for (const BadgePlace& place : placeBadges(s)) {
            const std::optional<render::BadgeIcon> icon = badgeIconOf(place.icon);
            if (!icon) {
                continue;
            }
            const QColor& ink =
                conflicting.contains(place.constraint) ? colors.chrome.conflict : colors.viewport.annotation;
            const QColor& border = m_selection.contains(place.constraint) ? colors.chrome.accent : colors.chrome.border;
            m_viewConstraints.annotations.badges.push_back({.anchor = vecOf(at(place.center(wpp))),
                                                            .offset = {},
                                                            .icon = *icon,
                                                            .iconColor = colorOf(ink),
                                                            .fill = colorOf(fill),
                                                            .border = colorOf(border)});
        }
    }
    if (!m_display.dimensions) {
        return;
    }
    const snapshot::Color halo = colorOf(colors.viewport.halo);
    const Vec2 middle = sketch::dimensionMiddle(s);
    for (const auto& [id, constraint] : s.constraints()) {
        if (!sketch::isDimension(constraint.kind)) {
            continue;
        }
        const std::optional<sketch::DimensionLayout> layout = sketch::layoutDimension(s, constraint, wpp, middle);
        if (!layout) {
            continue;
        }
        const bool selected = m_selection.contains(id);
        const QColor& color = selected                   ? colors.chrome.accent
                              : conflicting.contains(id) ? colors.chrome.conflictText
                                                         : colors.viewport.annotation;
        const snapshot::Color ink = colorOf(color);
        const auto addLine = [&](snapshot::Polyline points) {
            m_viewConstraints.halos.push_back(
                {.points = points, .color = halo, .width = style::kStrokeHalo, .pattern = render::kSolidPattern});
            m_viewConstraints.lines.push_back({.points = std::move(points),
                                               .color = ink,
                                               .width = style::kStrokeThin,
                                               .pattern = render::kSolidPattern});
        };
        for (const sketch::DimensionSegment& line : layout->lines) {
            addLine({at(line.from), at(line.to)});
        }
        if (layout->arc.size() > 1) {
            snapshot::Polyline arc;
            arc.reserve(layout->arc.size());
            for (const Vec2 p : layout->arc) {
                arc.push_back(at(p));
            }
            addLine(std::move(arc));
        }
        for (const sketch::DimensionArrow& arrow : layout->arrows) {
            m_viewConstraints.arrows.push_back(
                {.corners = {at(arrow.tip), at(arrow.left), at(arrow.right)}, .color = ink});
        }
        if (id == m_editingLabel) {
            m_editedLabel = labelRect(*layout);
            continue;
        }
        m_viewConstraints.annotations.labels.push_back(
            {.anchor = vecOf(at(layout->text)),
             .offset = {},
             .text = QString::fromStdString(layout->label).toStdU32String(),
             .color = ink,
             .fill = colorOf(colors.viewport.canvas),
             .border = colorOf(selected ? colors.chrome.accent : colors.chrome.border)});
    }
}

void SketchEditor::clearView()
{
    m_viewItems = {};
    m_viewConstraints = {};
    m_viewPreview.clear();
    m_viewPreviewMarkers.clear();
    publish();
    publishPreview();
}

void SketchEditor::publish()
{
    render::ActiveSketch active;
    active.profiles = m_viewItems.profiles;
    active.profileColor = colorOf(theme::palette().viewport.plane, kProfileTransparency);
    for (const std::vector<render::SketchStroke>* strokes :
         {&m_viewItems.halos, &m_viewConstraints.halos, &m_viewItems.lines, &m_viewConstraints.lines}) {
        active.strokes.insert(active.strokes.end(), strokes->begin(), strokes->end());
    }
    active.arrows = m_viewConstraints.arrows;
    m_newView->setActiveSketch(std::move(active));
    m_newView->setMarkers(m_viewItems.markers);
    m_newView->setAnnotations(m_viewConstraints.annotations);
}

void SketchEditor::publishPreview()
{
    m_newView->setSketchPreview({.strokes = m_viewPreview, .markers = m_viewPreviewMarkers});
}

void SketchEditor::onCameraChanged()
{
    if (!isActive()) {
        return;
    }
    const double wpp = worldPerPixel();
    if (wpp > 0.0 && (!(m_drawnScale > 0.0) || std::abs(wpp / m_drawnScale - 1.0) > kRescaleTolerance)) {
        relayoutView();
    }
}

} // namespace kalve137::app
