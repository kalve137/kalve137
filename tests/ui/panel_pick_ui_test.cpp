// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pair_shots.h"
#include "scenario.h"
#include "ui_test.h"

#include "app/shell/main_window.h"
#include "app/shell/startup_options.h"
#include "app/view/panel_picks.h"
#include "app/view/viewport_picker.h"
#include "render/chunk_set.h"
#include "render/draw_list.h"
#include "render/rhi/viewport.h"
#include "render/style.h"
#include "viewer/occt_view_widget.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QString>

#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr int kReach = 3;
constexpr int kMinLift = 30;
constexpr int kGoldTolerance = 15;
constexpr int kRuleTolerance = 6;
constexpr QPointF kAway{6.0, 6.0};

enum class Shows { Edge, Face, Body };

struct Case {
    const char* name;
    const char* command;
    gp_Pnt at;
    Shows shows;
    bool oldPreviewCovers = false;
};

const gp_Pnt kTopFrontEdge(0, -kBlockDepth / 2, kBlockHeight);
const gp_Pnt kTopFace(5, 3, kBlockHeight);

render::Encoded encodedOf(QRgb color)
{
    return {static_cast<float>(qRed(color)) / 255.0F, static_cast<float>(qGreen(color)) / 255.0F,
            static_cast<float>(qBlue(color)) / 255.0F};
}

QRgb rgbOf(const render::Encoded& color)
{
    return QColor::fromRgbF(color[0], color[1], color[2]).rgb();
}

std::vector<QPoint> around(const QImage& image, QPointF at)
{
    const double ratio = image.devicePixelRatio();
    const QPoint center((at * ratio).toPoint());
    std::vector<QPoint> pixels;
    for (int dy = -kReach; dy <= kReach; ++dy) {
        for (int dx = -kReach; dx <= kReach; ++dx) {
            if (const QPoint pixel = center + QPoint(dx, dy); image.rect().contains(pixel)) {
                pixels.push_back(pixel);
            }
        }
    }
    return pixels;
}

int liftAt(const QImage& plain, const QImage& picked, QPointF at)
{
    int lift = 0;
    for (const QPoint& pixel : around(picked, at)) {
        lift = std::max(lift, gapOf(plain.pixel(pixel), picked.pixel(pixel)));
    }
    return lift;
}

QRgb towardGold(QRgb color, float weight)
{
    const render::Encoded gold = render::encodedOf(render::kSelectedGold);
    render::Encoded mixed = encodedOf(color);
    for (std::size_t c = 0; c < mixed.size(); ++c) {
        mixed.at(c) += (gold.at(c) - mixed.at(c)) * weight;
    }
    return rgbOf(mixed);
}

QRgb medianAt(const QImage& image, QPointF at)
{
    std::vector<QRgb> colors;
    for (const QPoint& pixel : around(image, at)) {
        colors.push_back(image.pixel(pixel));
    }
    return median(colors);
}

int nearestTo(const QImage& image, QPointF at, QRgb target)
{
    int best = 255;
    for (const QPoint& pixel : around(image, at)) {
        best = std::min(best, gapOf(image.pixel(pixel), target));
    }
    return best;
}

void startPick(UiDriver& driver, const Case& pick)
{
    driver.trigger(QString::fromLatin1(pick.command));
    ASSERT_TRUE(driver.state()["featurePanel"]) << pick.name;
}

void pickAndLeave(UiDriver& driver, const Case& pick)
{
    driver.click(pick.at);
    driver.move(kAway);
}

using PanelPickPairTest = UiTest;

TEST_F(PanelPickPairTest, ShowsPickedEdgesFacesAndBodiesWhereTheOldViewDoes)
{
    if (ui->viewport() == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    UiDriver old(app::StartupOptions{.view = app::ViewKind::Old, .graphicsApi = std::nullopt});
    const bool oldColors = drawsReferenceColors(old.view());
    auto* picker = ui->window().findChild<app::ViewportPicker*>();
    ASSERT_NE(picker, nullptr);
    const QRgb gold = rgbOf(render::encodedOf(render::kSelectedGold));
    for (UiDriver* driver : {ui.get(), &old}) {
        block(*driver);
    }
    for (const Case& pick :
         {Case{"fillet", "Fillet", kTopFrontEdge, Shows::Edge}, Case{"shell", "Shell", kTopFace, Shows::Face},
          Case{"press pull", "Press Pull", kTopFace, Shows::Face, true}, Case{"move", "Move", kTopFace, Shows::Body},
          Case{"pattern", "Pattern", kTopFace, Shows::Body}}) {
        const std::string name = pick.name;
        for (UiDriver* driver : {ui.get(), &old}) {
            startPick(*driver, pick);
        }
        const QPointF at = ui->at(pick.at);
        const QImage plain = ui->canvasImage();
        const QImage oldPlain = old.canvasImage();
        for (UiDriver* driver : {ui.get(), &old}) {
            pickAndLeave(*driver, pick);
        }
        const QImage picked = ui->canvasImage();
        const QImage oldPicked = old.canvasImage();
        ui->save(name + " picked");

        const app::PanelItems& items = picker->panelItems();
        const render::Highlight& shown = ui->viewport()->chunks().highlight();
        const auto has = [](const std::set<render::PickedItem>& set, render::PickKind kind) {
            return std::ranges::any_of(set, [kind](const render::PickedItem& item) { return item.kind == kind; });
        };
        const render::BodyMeshes& overlaid = ui->viewport()->chunks().overlaid();
        const bool consumed = std::ranges::any_of(items.selected, [&](const render::PickedItem& item) {
            return overlaid.contains({item.feature, item.part});
        });
        switch (pick.shows) {
        case Shows::Edge:
            EXPECT_TRUE(has(items.selected, render::PickKind::Edge)) << name;
            break;
        case Shows::Face:
            EXPECT_TRUE(has(items.selected, render::PickKind::Face)) << name;
            EXPECT_FALSE(items.selectedOutline.empty()) << name;
            break;
        case Shows::Body:
            EXPECT_TRUE(has(items.bodies, render::PickKind::Face)) << name;
            EXPECT_TRUE(has(items.bodyOutline, render::PickKind::Edge)) << name;
            break;
        }
        EXPECT_TRUE(std::ranges::includes(shown.selected, items.selected)) << name;
        EXPECT_TRUE(std::ranges::includes(shown.hovered, items.bodies)) << name;

        const int lift = liftAt(plain, picked, at);
        const int oldLift = liftAt(oldPlain, oldPicked, old.at(pick.at));
        int rule = 0;
        switch (pick.shows) {
        case Shows::Edge:
            rule = nearestTo(picked, at, gold);
            EXPECT_LE(rule, kGoldTolerance) << name << ": the picked edge draws in selection gold";
            break;
        case Shows::Face:
        case Shows::Body: {
            const bool selected = pick.shows == Shows::Face;
            QRgb expected = rgbOf(render::highlighted(encodedOf(medianAt(plain, at)), !selected, selected));
            if (consumed) {
                const render::Highlight full = shown;
                render::Highlight bare = full;
                bare.selected.clear();
                bare.selectedOutline.clear();
                ui->viewport()->setHighlight(bare);
                const QRgb behind = medianAt(ui->canvasImage(), at);
                ui->viewport()->setHighlight(full);
                expected = towardGold(behind, pick.oldPreviewCovers ? render::kHiddenFaceWeight : render::kSelectedMix);
            }
            rule = gapOf(expected, medianAt(picked, at));
            EXPECT_LE(rule, kRuleTolerance)
                << name << ": expected " << hex(expected) << ", drew " << hex(medianAt(picked, at));
            break;
        }
        }
        std::printf("%s: viewport lift %d, old view lift %d, rule gap %d\n", name.c_str(), lift, oldLift, rule);
        EXPECT_GE(lift, kMinLift) << name << ": the viewport shows the pick";
        if (oldColors && !pick.oldPreviewCovers) {
            EXPECT_GE(oldLift, kMinLift) << name << ": the old view shows the pick";
        }

        for (UiDriver* driver : {ui.get(), &old}) {
            driver->frame();
            driver->key(Qt::Key_Escape);
            ASSERT_FALSE(driver->state()["featurePanel"]) << name << (driver == &old ? " on the old view" : "");
        }
        EXPECT_EQ(picker->panelItems(), app::PanelItems{}) << name << ": cancel drops the picks";
        EXPECT_TRUE(ui->viewport()->chunks().highlight().selected.empty()) << name;
        EXPECT_TRUE(ui->viewport()->chunks().highlight().hovered.empty()) << name;
    }
}

TEST_F(PanelPickPairTest, ShowsThePickedProfileUntilThePanelCloses)
{
    if (ui->viewport() == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    sketchRectangle(*ui, core::OriginPlane::XY, gp_Pnt(-20, -10, 0), gp_Pnt(20, 10, 0));
    ui->trigger("Extrude");
    ASSERT_TRUE(ui->state()["featurePanel"]);
    EXPECT_EQ(ui->viewport()->sketchLook().highlighted.size(), 1U);
    ui->key(Qt::Key_Escape);
    ASSERT_FALSE(ui->state()["featurePanel"]);
    EXPECT_TRUE(ui->viewport()->sketchLook().highlighted.empty());
}

} // namespace
} // namespace kalve137::uitest
