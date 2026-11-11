// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <AIS_InteractiveObject.hxx>
#include <Quantity_Color.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#include <optional>
#include <string>

namespace kalve137::viewer {

struct GridExtent {
    gp_Pnt center;
    double radius = 0.0;
};

struct GridLayout {
    double step = 0.0;
    gp_Pnt2d center;
    double radius = 0.0;
};

double niceGridStep(double minimum);

double gridStep(bool adaptive, double spacing, double minimum);

double roundToIncrement(double value, double increment);

GridLayout layoutGrid(const gp_Ax3& plane, double step, const gp_Pnt& viewTarget, double viewRadius,
                      const std::optional<GridExtent>& extent);

bool isGridStale(const GridLayout& current, const GridLayout& wanted);

struct GridStyle {
    Quantity_Color background;
    Quantity_Color minor;
    Quantity_Color major;
    Quantity_Color axisX;
    Quantity_Color axisY;
    Quantity_Color axisZ;
    Quantity_Color label;
    std::string labelFont;
};

class GridObject : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTI_INLINE(GridObject, AIS_InteractiveObject)

public:
    static constexpr int kMajorEvery = 5;

    enum class Part { Lines, Wash };

    explicit GridObject(Part part = Part::Lines);

    void setStyle(const GridStyle& style) { m_style = style; }
    void setPlane(const gp_Ax3& plane) { m_plane = plane; }
    const gp_Ax3& plane() const { return m_plane; }
    void setLabels(bool labels) { m_labels = labels; }
    void setLayout(double step, const gp_Pnt2d& center, double radius);

    double step() const { return m_step; }
    const gp_Pnt2d& center() const { return m_center; }
    double radius() const { return m_radius; }
    GridLayout layout() const { return {m_step, m_center, m_radius}; }

    bool AcceptDisplayMode(int mode) const override { return mode == 0; }

private:
    void Compute(const Handle(PrsMgr_PresentationManager)& manager, const Handle(Prs3d_Presentation)& presentation,
                 int mode) override;
    void ComputeSelection(const Handle(SelectMgr_Selection)&, int) override {}

    void computeLines(const Handle(Prs3d_Presentation)& presentation) const;
    void computeWash(const Handle(Prs3d_Presentation)& presentation) const;
    void computeLabels(const Handle(Prs3d_Presentation)& presentation) const;
    gp_Pnt toWorld(double u, double v) const;
    Quantity_Color faded(const Quantity_Color& color, double u, double v) const;
    Quantity_Color axisColor(const gp_Dir& direction) const;

    Part m_part;
    GridStyle m_style;
    gp_Ax3 m_plane;
    gp_Pnt2d m_center;
    double m_step = 0.0;
    double m_radius = 0.0;
    bool m_labels = false;
};

} // namespace kalve137::viewer
