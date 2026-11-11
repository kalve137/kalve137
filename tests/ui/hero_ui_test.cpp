// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "app/shell/main_window.h"
#include "viewer/occt_view_widget.h"

#include <AIS_InteractiveContext.hxx>
#include <Graphic3d_Camera.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Trsf.hxx>

#include <QImage>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <numbers>

namespace kalve137::uitest {
namespace {

using HeroTest = UiTest;

constexpr int kFrames = 120;
constexpr int kWindowWidth = 1048;
constexpr int kWindowHeight = 655;
constexpr double kZoom = 1.5;

TEST_F(HeroTest, TurnsTheDodecahedronAllTheWayRound)
{
    const char* target = std::getenv("KALVE137_HERO_FRAMES");
    if (target == nullptr) {
        GTEST_SKIP() << "Set KALVE137_HERO_FRAMES to a folder to record the README hero frames";
    }
    const std::filesystem::path folder(target);
    std::filesystem::create_directories(folder);

    ui->window().resize(kWindowWidth, kWindowHeight);
    ui->settle();
    ui->window().openDesignAt(std::filesystem::path(KALVE137_SOURCE_DIR) / "assets" / "samples" / "dodecahedron.k137");
    ui->settle();
    ASSERT_EQ(ui->state()["bodies"].size(), 1U) << ui->state().dump(2);

    const Handle(V3d_View) view = ui->view().aisContext()->CurrentViewer()->ActiveViews().First();
    view->SetZoom(kZoom);
    gp_Trsf step;
    step.SetRotation(gp_Ax1(gp::Origin(), gp::DZ()), 2.0 * std::numbers::pi / kFrames);
    for (int i = 0; i < kFrames; ++i) {
        const QImage frame = ui->shot();
        ASSERT_TRUE(frame.save(QString::fromStdU16String((folder / std::format("frame-{:03}.png", i)).u16string())));
        view->Camera()->Transform(step);
        view->Invalidate();
        ui->view().redraw();
    }
}

} // namespace
} // namespace kalve137::uitest
