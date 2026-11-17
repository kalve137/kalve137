// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <AIS_ViewCube.hxx>
#include <Quantity_Color.hxx>

namespace kalve137::viewer {

class ViewCube : public AIS_ViewCube {
    DEFINE_STANDARD_RTTI_INLINE(ViewCube, AIS_ViewCube)

public:
    void setHoverLabelColor(const Quantity_Color& color) { m_hoverLabelColor = color; }

    void HilightOwnerWithColor(const Handle(PrsMgr_PresentationManager)& manager, const Handle(Prs3d_Drawer)& style,
                               const Handle(SelectMgr_EntityOwner)& owner) override;

private:
    Quantity_Color m_hoverLabelColor;
};

} // namespace kalve137::viewer
