// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/measure_panel.h"

#include "app/theme/theme.h"

#include "geometry/measure.h"
#include "viewer/occt_view_widget.h"
#include "viewer/style.h"

#include <OcctQtTools.h>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace kalve137::app {

namespace style = viewer::style;

namespace {

constexpr double kSameSpot = 1e-9;

Quantity_Color occt(const QColor& color)
{
    return OcctQtTools::qtColorToOcct(color);
}

QString mm(double value)
{
    return QStringLiteral("%1 mm").arg(value, 0, 'f', 3);
}

QString row(const QString& name, const QString& value)
{
    return QStringLiteral("<tr><td style='color:%1; padding-right:%2px'>%3</td><td>%4</td></tr>")
        .arg(theme::kTitanium.name())
        .arg(theme::kSpaceL)
        .arg(name, value);
}

QString nameOf(const Pick& pick)
{
    switch (pick.kind) {
    case Pick::Kind::Face:
        return MeasurePanel::tr("Face");
    case Pick::Kind::Edge:
        return MeasurePanel::tr("Edge");
    case Pick::Kind::Vertex:
        return MeasurePanel::tr("Vertex");
    case Pick::Kind::OriginPlane:
    case Pick::Kind::Profile:
    case Pick::Kind::SketchLine:
    case Pick::Kind::SketchPoint:
    case Pick::Kind::ConstructionPlane:
    case Pick::Kind::OriginAxis:
    case Pick::Kind::ConstructionAxis:
    case Pick::Kind::ConstructionPoint:
        return MeasurePanel::tr("Item");
    }
    return {};
}

} // namespace

MeasurePanel::MeasurePanel(Scene& scene, viewer::OcctViewWidget* view, QWidget* parent)
    : QWidget(parent), m_scene(scene), m_view(view)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(theme::kPanelMargins);
    auto* title = new QLabel(tr("Measure"), this);
    title->setObjectName("panelTitle");
    layout->addWidget(title);
    m_selection = new QLabel(this);
    m_selection->setObjectName("panelHint");
    m_selection->setWordWrap(true);
    layout->addWidget(m_selection);
    m_results = new QLabel(this);
    m_results->setTextFormat(Qt::RichText);
    m_results->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_results);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    auto* restart = new QPushButton(tr("Restart"), this);
    connect(restart, &QPushButton::clicked, this, [this] {
        m_picks.clear();
        refresh();
    });
    buttons->addWidget(restart);
    auto* close = new QPushButton(tr("Close"), this);
    close->setObjectName("primaryButton");
    connect(close, &QPushButton::clicked, this, &MeasurePanel::stop);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(view, &viewer::OcctViewWidget::selectionChanged, this, &MeasurePanel::onSelectionChanged);
}

void MeasurePanel::start()
{
    m_active = true;
    m_picks.clear();
    m_scene.setPickMode(PickMode::Measure);
    refresh();
    emit activeChanged(true);
}

void MeasurePanel::stop()
{
    if (!m_active) {
        return;
    }
    m_active = false;
    m_picks.clear();
    clearOverlay();
    m_scene.setPickMode(PickMode::None);
    m_view->redraw();
    emit activeChanged(false);
}

void MeasurePanel::onSelectionChanged()
{
    if (!m_active) {
        return;
    }
    const std::vector<Pick> picks = m_scene.takePicks();
    if (picks.empty()) {
        m_picks.clear();
    }
    for (const Pick& pick : picks) {
        const auto same = std::ranges::find_if(m_picks, [&](const Pick& p) { return p.shape.IsSame(pick.shape); });
        if (same != m_picks.end()) {
            m_picks.erase(same);
            continue;
        }
        if (m_picks.size() == 2) {
            m_picks.clear();
        }
        m_picks.push_back(pick);
    }
    refresh();
}

void MeasurePanel::refresh()
{
    clearOverlay();
    if (m_picks.empty()) {
        m_selection->setText(tr("Click a face, edge or vertex. Click a second one to measure between them."));
        m_results->clear();
        m_view->redraw();
        return;
    }

    QStringList names;
    for (const Pick& pick : m_picks) {
        names << nameOf(pick);
        Handle(AIS_Shape) highlight = new AIS_Shape(pick.shape);
        highlight->SetColor(occt(theme::kGold));
        highlight->SetWidth(style::kStrokeBold);
        highlight->Attributes()->SetPointAspect(
            new Prs3d_PointAspect(Aspect_TOM_BALL, occt(theme::kGold), style::kMarkerSmall));
        highlight->SetTransparency(style::kTintStrong);
        highlight->SetPolygonOffsets(Aspect_POM_Fill, -1.0f, -1.0f);
        showOverlay(highlight);
    }
    m_selection->setText(names.join(QStringLiteral(" + ")));

    QString table;
    if (m_picks.size() == 1) {
        const geometry::Measurement m = geometry::measure(m_picks.front().shape);
        if (m.length) {
            table += row(tr("Length"), mm(*m.length));
        }
        if (m.area) {
            table += row(tr("Area"), QStringLiteral("%1 mm\u00B2").arg(*m.area, 0, 'f', 3));
        }
        if (m.radius) {
            table += row(tr("Radius"), mm(*m.radius));
            table += row(tr("Diameter"), mm(*m.radius * 2.0));
        }
        if (m.position) {
            table += row(QStringLiteral("X"), mm(m.position->X()));
            table += row(QStringLiteral("Y"), mm(m.position->Y()));
            table += row(QStringLiteral("Z"), mm(m.position->Z()));
        }
    } else {
        const geometry::Measurement m = geometry::measure(m_picks[0].shape, m_picks[1].shape);
        if (m.distance) {
            table += row(tr("Distance"), mm(*m.distance));
            const gp_Vec d(m.from, m.to);
            table += row(QStringLiteral("\u0394X"), mm(std::abs(d.X())));
            table += row(QStringLiteral("\u0394Y"), mm(std::abs(d.Y())));
            table += row(QStringLiteral("\u0394Z"), mm(std::abs(d.Z())));
            if (*m.distance > kSameSpot) {
                Handle(AIS_Shape) line = new AIS_Shape(BRepBuilderAPI_MakeEdge(m.from, m.to));
                line->SetColor(occt(theme::kGold));
                line->Attributes()->SetWireAspect(
                    new Prs3d_LineAspect(occt(theme::kGold), Aspect_TOL_DASH, style::kStrokeRegular));
                line->SetZLayer(Graphic3d_ZLayerId_Topmost);
                showOverlay(line);
                for (const gp_Pnt& end : {m.from, m.to}) {
                    Handle(AIS_Shape) dot = new AIS_Shape(BRepBuilderAPI_MakeVertex(end));
                    dot->Attributes()->SetPointAspect(
                        new Prs3d_PointAspect(Aspect_TOM_BALL, occt(theme::kGold), style::kMarkerSmall));
                    dot->SetZLayer(Graphic3d_ZLayerId_Topmost);
                    showOverlay(dot);
                }
            }
        }
        if (m.angle) {
            table += row(tr("Angle"), QStringLiteral("%1\u00B0").arg(*m.angle, 0, 'f', 2));
        }
    }
    m_results->setText(QStringLiteral("<table>%1</table>").arg(table));
    m_view->redraw();
}

void MeasurePanel::clearOverlay()
{
    for (const Handle(AIS_InteractiveObject)& object : m_overlay) {
        m_view->aisContext()->Remove(object, false);
    }
    m_overlay.clear();
}

void MeasurePanel::showOverlay(const Handle(AIS_InteractiveObject)& object)
{
    m_view->aisContext()->Display(object, AIS_Shaded, -1, false);
    m_overlay.push_back(object);
}

} // namespace kalve137::app
