// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/grid.h"

#include "viewer/style.h"

#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_Text.hxx>
#include <Prs3d_TextAspect.hxx>
#include <TCollection_ExtendedString.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <vector>

namespace kalve137::viewer {

namespace {

constexpr double kLabelRadius = 0.5;
constexpr double kLabelGap = 0.4;
constexpr double kLabelHeight = 12.0;
constexpr double kAxisLineWidth = style::kStrokeThin;
constexpr int kWashSegments = 96;
constexpr float kWashTransparency = static_cast<float>(style::kTintHalf);
constexpr float kWashOffsetFactor = 2.0f;
constexpr float kWashOffsetUnits = 4.0f;

camera::Vec3 toVec(const gp_XYZ& xyz)
{
    return {xyz.X(), xyz.Y(), xyz.Z()};
}

std::string formatValue(double value, double step)
{
    constexpr int kMaxDecimals = 6;
    int decimals = std::max(0, static_cast<int>(std::ceil(-std::log10(step))));
    while (decimals < kMaxDecimals) {
        const double scaled = step * std::pow(10.0, decimals);
        if (std::abs(scaled - std::round(scaled)) < 1e-6 * scaled) {
            break;
        }
        ++decimals;
    }
    std::string text = std::format("{:.{}f}", value, decimals);
    if (text.find('.') != std::string::npos) {
        text.erase(text.find_last_not_of('0') + 1);
        if (text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

} // namespace

double roundToIncrement(double value, double increment)
{
    return increment > 0.0 ? std::round(value / increment) * increment : value;
}

render::GridPlane gridPlaneOf(const gp_Ax3& plane)
{
    return {toVec(plane.Location().XYZ()), toVec(plane.XDirection().XYZ()), toVec(plane.YDirection().XYZ()),
            toVec(plane.Direction().XYZ())};
}

render::GridExtent gridExtentOf(const GridExtent& extent)
{
    return {toVec(extent.center.XYZ()), extent.radius};
}

GridObject::GridObject(Part part) : m_part(part) {}

void GridObject::Compute(const Handle(PrsMgr_PresentationManager)&, const Handle(Prs3d_Presentation)& presentation,
                         int mode)
{
    presentation->SetInfiniteState(true);
    if (mode != 0 || m_layout.step <= 0.0 || m_layout.radius <= 0.0) {
        return;
    }
    switch (m_part) {
    case Part::Lines:
        computeLines(presentation);
        if (m_labels) {
            computeLabels(presentation);
        }
        break;
    case Part::Wash:
        computeWash(presentation);
        break;
    }
}

void GridObject::computeWash(const Handle(Prs3d_Presentation)& presentation) const
{
    Handle(Graphic3d_ArrayOfTriangles) fan = new Graphic3d_ArrayOfTriangles(kWashSegments * 3);
    const render::GridPoint& middle = m_layout.center;
    const double radius = m_layout.radius;
    const gp_Pnt center = toWorld(middle.u, middle.v);
    for (int i = 0; i < kWashSegments; ++i) {
        const double a = 2.0 * std::numbers::pi * i / kWashSegments;
        const double b = 2.0 * std::numbers::pi * (i + 1) / kWashSegments;
        fan->AddVertex(center);
        fan->AddVertex(toWorld(middle.u + radius * std::cos(a), middle.v + radius * std::sin(a)));
        fan->AddVertex(toWorld(middle.u + radius * std::cos(b), middle.v + radius * std::sin(b)));
    }
    Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_UserDefined);
    material.SetColor(m_style.background);
    material.SetTransparency(kWashTransparency);
    Handle(Graphic3d_AspectFillArea3d) aspect = new Graphic3d_AspectFillArea3d();
    aspect->SetInteriorStyle(Aspect_IS_SOLID);
    aspect->SetInteriorColor(Quantity_ColorRGBA(m_style.background, 1.0f - kWashTransparency));
    aspect->SetFrontMaterial(material);
    aspect->SetBackMaterial(material);
    aspect->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    aspect->SetAlphaMode(Graphic3d_AlphaMode_Blend);
    aspect->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
    aspect->SetPolygonOffsets(Aspect_POM_Fill, kWashOffsetFactor, kWashOffsetUnits);
    Handle(Graphic3d_Group) group = presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect);
    group->AddPrimitiveArray(fan);
}

void GridObject::computeLines(const Handle(Prs3d_Presentation)& presentation) const
{
    std::vector<render::GridLine> minor;
    std::vector<render::GridLine> major;
    std::vector<render::GridLine> axes;
    for (const render::GridLine& line : render::gridLines(m_layout)) {
        switch (line.kind) {
        case render::GridLineKind::Minor:
            minor.push_back(line);
            break;
        case render::GridLineKind::Major:
            major.push_back(line);
            break;
        case render::GridLineKind::Axis:
            axes.push_back(line);
            break;
        }
    }

    const auto addGroup = [&](const std::vector<render::GridLine>& lines, double width,
                              const Quantity_Color* fixedColor) {
        if (lines.empty()) {
            return;
        }
        Handle(Graphic3d_ArrayOfSegments) segments = new Graphic3d_ArrayOfSegments(
            static_cast<int>(lines.size()) * render::kGridPiecesPerLine * 2, 0, Graphic3d_ArrayFlags_VertexColor);
        for (const render::GridLine& line : lines) {
            const Quantity_Color color = fixedColor != nullptr
                                             ? *fixedColor
                                             : axisColor(line.alongV ? m_plane.YDirection() : m_plane.XDirection());
            const double start = line.start;
            const double piece = (line.end - line.start) / render::kGridPiecesPerLine;
            for (int p = 0; p < render::kGridPiecesPerLine; ++p) {
                for (const double along : {start + p * piece, start + (p + 1) * piece}) {
                    const double u = line.alongV ? line.offset : along;
                    const double v = line.alongV ? along : line.offset;
                    segments->AddVertex(toWorld(u, v), faded(color, u, v));
                }
            }
        }
        Handle(Graphic3d_Group) group = presentation->NewGroup();
        group->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(m_style.minor, Aspect_TOL_SOLID, width));
        group->AddPrimitiveArray(segments);
    };
    addGroup(minor, style::kStrokeHairline, &m_style.minor);
    addGroup(major, style::kStrokeHairline, &m_style.major);
    addGroup(axes, kAxisLineWidth, nullptr);
}

void GridObject::computeLabels(const Handle(Prs3d_Presentation)& presentation) const
{
    const double step = m_layout.step;
    const double majorStep = step * render::kGridMajorEvery;
    const double reach = m_layout.radius * kLabelRadius;
    const double gap = step * kLabelGap;
    Handle(Graphic3d_Group) group = presentation->NewGroup();

    Handle(Prs3d_TextAspect) below = new Prs3d_TextAspect();
    below->SetColor(m_style.label);
    below->SetFont(m_style.labelFont.c_str());
    below->SetHeight(kLabelHeight);
    below->SetHorizontalJustification(Graphic3d_HTA_CENTER);
    below->SetVerticalJustification(Graphic3d_VTA_TOP);
    Handle(Prs3d_TextAspect) beside = new Prs3d_TextAspect();
    beside->SetColor(m_style.label);
    beside->SetFont(m_style.labelFont.c_str());
    beside->SetHeight(kLabelHeight);
    beside->SetHorizontalJustification(Graphic3d_HTA_RIGHT);
    beside->SetVerticalJustification(Graphic3d_VTA_CENTER);

    const auto range = [&](double center) {
        return std::pair{static_cast<long long>(std::ceil((center - reach) / majorStep)),
                         static_cast<long long>(std::floor((center + reach) / majorStep))};
    };
    const render::GridPoint& center = m_layout.center;
    if (std::abs(center.v) < reach) {
        const auto [first, last] = range(center.u);
        for (long long i = first; i <= last; ++i) {
            if (i == 0) {
                continue;
            }
            const double u = static_cast<double>(i) * majorStep;
            const TCollection_ExtendedString text(formatValue(u, step).c_str());
            Prs3d_Text::Draw(group, below, text, toWorld(u, -gap));
        }
    }
    if (std::abs(center.u) < reach) {
        const auto [first, last] = range(center.v);
        for (long long i = first; i <= last; ++i) {
            if (i == 0) {
                continue;
            }
            const double v = static_cast<double>(i) * majorStep;
            const TCollection_ExtendedString text(formatValue(v, step).c_str());
            Prs3d_Text::Draw(group, beside, text, toWorld(-gap, v));
        }
    }
}

gp_Pnt GridObject::toWorld(double u, double v) const
{
    return m_plane.Location().Translated(gp_Vec(m_plane.XDirection()) * u).Translated(gp_Vec(m_plane.YDirection()) * v);
}

Quantity_Color GridObject::faded(const Quantity_Color& color, double u, double v) const
{
    const double w = render::gridFade(m_layout, u, v);
    const Quantity_Color& to = m_style.background;
    return Quantity_Color(color.Red() + (to.Red() - color.Red()) * w, color.Green() + (to.Green() - color.Green()) * w,
                          color.Blue() + (to.Blue() - color.Blue()) * w, Quantity_TOC_RGB);
}

Quantity_Color GridObject::axisColor(const gp_Dir& direction) const
{
    const double x = std::abs(direction.X());
    const double y = std::abs(direction.Y());
    const double z = std::abs(direction.Z());
    if (x >= y && x >= z) {
        return m_style.axisX;
    }
    return y >= z ? m_style.axisY : m_style.axisZ;
}

} // namespace kalve137::viewer
