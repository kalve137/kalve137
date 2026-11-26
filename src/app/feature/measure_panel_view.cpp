// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/measure_panel.h"
#include "app/theme/theme.h"
#include "app/view/panel_picks.h"
#include "app/view/view_projection.h"
#include "app/view/viewport_picker.h"
#include "render/measure.h"
#include "render/rhi/viewport.h"

#include <TopoDS.hxx>

#include <QColor>

#include <array>
#include <utility>

namespace kalve137::app {

namespace {

snapshot::Color colorOf(const QColor& color)
{
    return {static_cast<float>(color.redF()), static_cast<float>(color.greenF()), static_cast<float>(color.blueF()),
            static_cast<float>(color.alphaF())};
}

camera::Vec3 vecOf(const gp_Pnt& point)
{
    return {point.X(), point.Y(), point.Z()};
}

} // namespace

void MeasurePanel::publish(const Span& span)
{
    PanelPicks picks;
    for (const Pick& pick : m_picks) {
        switch (pick.kind) {
        case Pick::Kind::Face:
            picks.faces.push_back(TopoDS::Face(pick.shape));
            break;
        case Pick::Kind::Edge:
            picks.edges.push_back(TopoDS::Edge(pick.shape));
            break;
        case Pick::Kind::Vertex:
            picks.vertices.push_back(TopoDS::Vertex(pick.shape));
            break;
        case Pick::Kind::OriginPlane:
        case Pick::Kind::Profile:
        case Pick::Kind::SketchLine:
        case Pick::Kind::SketchPoint:
        case Pick::Kind::ConstructionPlane:
        case Pick::Kind::OriginAxis:
        case Pick::Kind::ConstructionAxis:
        case Pick::Kind::ConstructionPoint:
            break;
        }
    }
    if (auto* picker = qobject_cast<ViewportPicker*>(&m_source)) {
        picker->setPanelPicks(std::move(picks));
    }

    render::MeasureItems items;
    if (span) {
        const snapshot::Color accent = colorOf(theme::palette().chrome.accent);
        items.line = render::MeasureLine{.from = vecOf(span->first), .to = vecOf(span->second), .color = accent};
        const std::array<std::pair<gp_Pnt, const Pick*>, 2> ends{
            {{span->first, &m_picks[0]}, {span->second, &m_picks[1]}}};
        for (const auto& [end, pick] : ends) {
            if (pick->kind == Pick::Kind::Vertex) {
                continue;
            }
            items.markers.push_back({.anchor = vecOf(end),
                                     .shape = render::MarkerShape::Ball,
                                     .scale = render::MarkerScale::Small,
                                     .color = accent,
                                     .flags = render::kMarkerTopmost});
        }
    }
    m_projection.viewport()->setMeasure(std::move(items));
}

} // namespace kalve137::app
