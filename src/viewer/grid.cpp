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

constexpr int kPiecesPerLine = 24;
constexpr double kFadeStart = 0.35;
constexpr double kLabelRadius = 0.5;
constexpr double kLabelGap = 0.4;
constexpr double kLabelHeight = 12.0;
constexpr double kAxisLineWidth = style::kStrokeThin;
constexpr int kWashSegments = 96;
constexpr float kWashTransparency = static_cast<float>(style::kTintHalf);
constexpr float kWashOffsetFactor = 2.0f;
constexpr float kWashOffsetUnits = 4.0f;
constexpr double kGridRecenterFraction = 0.1;
constexpr double kGridShrinkFraction = 0.6;

struct Line {
    bool alongV = false;
    double offset = 0.0;
};

double smoothstep(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
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

double niceGridStep(double minimum)
{
    const double decade = std::pow(10.0, std::floor(std::log10(minimum)));
    for (const double factor : {1.0, 2.0, 5.0}) {
        if (decade * factor >= minimum) {
            return decade * factor;
        }
    }
    return decade * 10.0;
}

double gridStep(bool adaptive, double spacing, double minimum)
{
    if (adaptive || spacing <= 0.0) {
        return niceGridStep(minimum);
    }
    double step = spacing;
    while (step < minimum) {
        step *= GridObject::kMajorEvery;
    }
    return step;
}

double roundToIncrement(double value, double increment)
{
    return increment > 0.0 ? std::round(value / increment) * increment : value;
}

GridLayout layoutGrid(const gp_Ax3& plane, double step, const gp_Pnt& viewTarget, double viewRadius,
                      const std::optional<GridExtent>& extent)
{
    double radius = viewRadius;
    gp_Pnt target = viewTarget;
    if (extent && extent->radius < viewRadius) {
        radius = extent->radius;
        target = extent->center;
    }
    const gp_Vec toTarget(plane.Location(), target);
    const double majorStep = step * GridObject::kMajorEvery;
    const gp_Pnt2d center(std::round(toTarget.Dot(gp_Vec(plane.XDirection())) / majorStep) * majorStep,
                          std::round(toTarget.Dot(gp_Vec(plane.YDirection())) / majorStep) * majorStep);
    return {step, center, radius};
}

bool isGridStale(const GridLayout& current, const GridLayout& wanted)
{
    return wanted.step != current.step || wanted.radius > current.radius ||
           wanted.radius < current.radius * kGridShrinkFraction ||
           wanted.center.Distance(current.center) > current.radius * kGridRecenterFraction;
}

GridObject::GridObject(Part part) : m_part(part) {}

void GridObject::setLayout(double step, const gp_Pnt2d& center, double radius)
{
    m_step = step;
    m_center = center;
    m_radius = radius;
}

void GridObject::Compute(const Handle(PrsMgr_PresentationManager)&, const Handle(Prs3d_Presentation)& presentation,
                         int mode)
{
    presentation->SetInfiniteState(true);
    if (mode != 0 || m_step <= 0.0 || m_radius <= 0.0) {
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
    const gp_Pnt center = toWorld(m_center.X(), m_center.Y());
    for (int i = 0; i < kWashSegments; ++i) {
        const double a = 2.0 * std::numbers::pi * i / kWashSegments;
        const double b = 2.0 * std::numbers::pi * (i + 1) / kWashSegments;
        fan->AddVertex(center);
        fan->AddVertex(toWorld(m_center.X() + m_radius * std::cos(a), m_center.Y() + m_radius * std::sin(a)));
        fan->AddVertex(toWorld(m_center.X() + m_radius * std::cos(b), m_center.Y() + m_radius * std::sin(b)));
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
    std::vector<Line> minor;
    std::vector<Line> major;
    std::vector<Line> axes;
    const int half = static_cast<int>(std::ceil(m_radius / m_step));
    for (const bool alongV : {true, false}) {
        const double center = alongV ? m_center.X() : m_center.Y();
        for (int i = -half; i <= half; ++i) {
            const double offset = center + i * m_step;
            if (std::abs(offset - center) >= m_radius) {
                continue;
            }
            const long long index = std::llround(offset / m_step);
            const Line line{alongV, static_cast<double>(index) * m_step};
            if (index == 0) {
                axes.push_back(line);
            } else if (index % kMajorEvery == 0) {
                major.push_back(line);
            } else {
                minor.push_back(line);
            }
        }
    }

    const auto addGroup = [&](const std::vector<Line>& lines, double width, const Quantity_Color* fixedColor) {
        if (lines.empty()) {
            return;
        }
        Handle(Graphic3d_ArrayOfSegments) segments = new Graphic3d_ArrayOfSegments(
            static_cast<int>(lines.size()) * kPiecesPerLine * 2, 0, Graphic3d_ArrayFlags_VertexColor);
        for (const Line& line : lines) {
            const Quantity_Color color = fixedColor != nullptr
                                             ? *fixedColor
                                             : axisColor(line.alongV ? m_plane.YDirection() : m_plane.XDirection());
            const double across = line.offset - (line.alongV ? m_center.X() : m_center.Y());
            const double reach = std::sqrt(std::max(0.0, m_radius * m_radius - across * across));
            const double start = (line.alongV ? m_center.Y() : m_center.X()) - reach;
            const double piece = 2.0 * reach / kPiecesPerLine;
            for (int p = 0; p < kPiecesPerLine; ++p) {
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
    const double majorStep = m_step * kMajorEvery;
    const double reach = m_radius * kLabelRadius;
    const double gap = m_step * kLabelGap;
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
    if (std::abs(m_center.Y()) < reach) {
        const auto [first, last] = range(m_center.X());
        for (long long i = first; i <= last; ++i) {
            if (i == 0) {
                continue;
            }
            const double u = static_cast<double>(i) * majorStep;
            const TCollection_ExtendedString text(formatValue(u, m_step).c_str());
            Prs3d_Text::Draw(group, below, text, toWorld(u, -gap));
        }
    }
    if (std::abs(m_center.X()) < reach) {
        const auto [first, last] = range(m_center.Y());
        for (long long i = first; i <= last; ++i) {
            if (i == 0) {
                continue;
            }
            const double v = static_cast<double>(i) * majorStep;
            const TCollection_ExtendedString text(formatValue(v, m_step).c_str());
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
    const double distance = std::hypot(u - m_center.X(), v - m_center.Y()) / m_radius;
    const double w = smoothstep((distance - kFadeStart) / (1.0 - kFadeStart));
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
