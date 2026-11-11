// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"

#include "viewer/occt_view_widget.h"

#include <gtest/gtest.h>

namespace kalve137::uitest {

void sketchRectangle(UiDriver& ui, core::OriginPlane plane, const gp_Pnt& corner, const gp_Pnt& opposite)
{
    ui.trigger("Create Sketch");
    ui.click(ui.originPlane(plane));
    ASSERT_NE(ui.state()["sketch"], 0) << ui.state().dump(2);
    ui.trigger("Rectangle");
    ui.click(corner);
    ui.click(opposite);
    ui.trigger("Finish Sketch");
}

void extrude(UiDriver& ui, const QString& distance)
{
    ui.trigger("Extrude");
    ASSERT_TRUE(ui.state()["featurePanel"]) << ui.state().dump(2);
    ui.fill("Distance", distance);
    ASSERT_FALSE(ui.state()["featurePanel"]) << ui.state().dump(2);
}

void block(UiDriver& ui)
{
    sketchRectangle(ui, core::OriginPlane::XY, gp_Pnt(-kBlockWidth / 2, -kBlockDepth / 2, 0),
                    gp_Pnt(kBlockWidth / 2, kBlockDepth / 2, 0));
    extrude(ui, QString::number(kBlockHeight));
    ASSERT_EQ(ui.state()["bodies"].size(), 1U) << ui.state().dump(2);
    homeView(ui);
}

void homeView(UiDriver& ui)
{
    ui.view().homeView();
    ui.view().fitAll();
    ui.settle();
}

double volume(const UiDriver& ui, std::size_t body)
{
    const nlohmann::json state = ui.state();
    return body < state["bodies"].size() ? state["bodies"][body]["volume"].get<double>() : 0.0;
}

int faces(const UiDriver& ui, std::size_t body)
{
    const nlohmann::json state = ui.state();
    return body < state["bodies"].size() ? state["bodies"][body]["faces"].get<int>() : 0;
}

} // namespace kalve137::uitest
