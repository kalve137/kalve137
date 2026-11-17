// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/view_cube.h"

#include <Graphic3d_AspectText3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_Text.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_TextAspect.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <TCollection_AsciiString.hxx>
#include <V3d.hxx>
#include <gp.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

namespace kalve137::viewer {

namespace {

constexpr double kAisViewCubeLabelLift = 2.0;

gp_Dir labelUp(V3d_TypeOfOrientation side, bool yUp)
{
    if (yUp) {
        return side == V3d_Ypos || side == V3d_Yneg ? -gp::DZ() : gp::DY();
    }
    if (side == V3d_Zpos) {
        return gp::DY();
    }
    return side == V3d_Zneg ? -gp::DY() : gp::DZ();
}

} // namespace

void ViewCube::HilightOwnerWithColor(const Handle(PrsMgr_PresentationManager)& manager,
                                     const Handle(Prs3d_Drawer)& style, const Handle(SelectMgr_EntityOwner)& owner)
{
    AIS_ViewCube::HilightOwnerWithColor(manager, style, owner);
    const auto* cubeOwner = dynamic_cast<const AIS_ViewCubeOwner*>(owner.get());
    if (cubeOwner == nullptr || !manager->IsImmediateModeOn() || !IsBoxSide(cubeOwner->MainOrientation())) {
        return;
    }
    const V3d_TypeOfOrientation side = cubeOwner->MainOrientation();
    const TCollection_AsciiString label = BoxSideLabel(side);
    if (label.IsEmpty()) {
        return;
    }
    const Handle(Prs3d_TextAspect)& textAspect = Attributes()->TextAspect();
    const Handle(Graphic3d_AspectText3d) aspect = new Graphic3d_AspectText3d(*textAspect->Aspect());
    aspect->SetColor(m_hoverLabelColor);
    const Handle(Graphic3d_Group) group = GetHilightPresentation(manager)->NewGroup();
    group->SetGroupPrimitivesAspect(aspect);

    const gp_Dir normal = V3d::GetProjAxis(side);
    const gp_Pnt position(normal.XYZ() * (Size() / 2.0 + BoxFacetExtension() + kAisViewCubeLabelLift));
    const Handle(Graphic3d_Text) text = new Graphic3d_Text(static_cast<float>(textAspect->Height()));
    text->SetText(label);
    text->SetOrientation(gp_Ax2(position, normal, labelUp(side, IsYup()).Crossed(normal)));
    text->SetOwnAnchorPoint(false);
    text->SetHorizontalAlignment(textAspect->HorizontalJustification());
    text->SetVerticalAlignment(textAspect->VerticalJustification());
    group->AddText(text);
}

} // namespace kalve137::viewer
