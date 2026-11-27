// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "viewport_ui_support.h"

#include "app/feature/value_edit.h"
#include "app/session/preferences.h"
#include "app/shell/startup_options.h"
#include "app/sketch/sketch_view_items.h"
#include "app/theme/theme.h"
#include "camera/camera.h"
#include "core/expression.h"
#include "render/handles.h"
#include "render/rhi/viewport.h"
#include "render/style.h"
#include "snapshot/body.h"
#include "viewer/occt_view_widget.h"

#include <QImage>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <string>

namespace kalve137::uitest {
namespace {

constexpr double kSizeTolerance = 1.0;
constexpr double kSampleStep = 0.05;
constexpr double kHalfCover = 0.5;
constexpr double kSearchBehind = 10.0;
constexpr double kSearchBeyond = 60.0;
constexpr double kHeadProbe = 6.25;
constexpr double kHeadNear = 3.0;
constexpr double kHeadFar = 10.0;
constexpr double kByteMax = 255.0;
constexpr double kDragPixels = 40.0;
constexpr int kDragSteps = 8;
constexpr double kInsidePixels = 9.0;
constexpr double kOutsidePixels = 11.0;
constexpr double kNear = 1e-6;
constexpr double kSameScale = 1.0;
constexpr int kZoomNotches = 6;

struct Cover {
    const QImage& plain;
    const QImage& shown;

    double pixel(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= shown.width() || y >= shown.height()) {
            return 0.0;
        }
        const int below = qRed(plain.pixel(x, y));
        const int room = static_cast<int>(kByteMax) - below;
        return room > 0 ? std::clamp(static_cast<double>(qRed(shown.pixel(x, y)) - below) / room, 0.0, 1.0) : 0.0;
    }

    double at(QPointF p) const
    {
        const double fx = p.x() - 0.5;
        const double fy = p.y() - 0.5;
        const auto x = static_cast<int>(std::floor(fx));
        const auto y = static_cast<int>(std::floor(fy));
        const double a = fx - x;
        const double b = fy - y;
        return ((pixel(x, y) * (1.0 - a) + pixel(x + 1, y) * a) * (1.0 - b)) +
               ((pixel(x, y + 1) * (1.0 - a) + pixel(x + 1, y + 1) * a) * b);
    }

    std::optional<double> rising(QPointF origin, QPointF along, double from, double to) const
    {
        const double step = to > from ? kSampleStep : -kSampleStep;
        const auto count = static_cast<int>(std::floor((to - from) / step));
        double previous = at(origin + along * from);
        for (int i = 1; i <= count; ++i) {
            const double t = from + (step * i);
            const double cover = at(origin + along * t);
            if (previous < kHalfCover && cover >= kHalfCover) {
                return t - (step * (cover - kHalfCover) / (cover - previous));
            }
            previous = cover;
        }
        return std::nullopt;
    }
};

struct ArrowSize {
    double start = 0.0;
    double shaft = 0.0;
    double head = 0.0;
};

std::optional<double> widthAt(const Cover& cover, QPointF base, QPointF along, double t, double ratio)
{
    const QPointF side(-along.y(), along.x());
    const QPointF center = base + along * t;
    const std::optional<double> left = cover.rising(center, side, kSearchBehind * ratio, 0.0);
    const std::optional<double> right = cover.rising(center, -side, kSearchBehind * ratio, 0.0);
    if (!left || !right) {
        return std::nullopt;
    }
    return *left + *right;
}

std::optional<ArrowSize> measure(const Cover& cover, QPointF base, QPointF along, double ratio)
{
    const QPointF side(-along.y(), along.x());
    const std::optional<double> start = cover.rising(base, along, -kSearchBehind * ratio, kSearchBehind * ratio);
    const std::optional<double> step =
        cover.rising(base + side * (kHeadProbe * ratio), along, -kSearchBehind * ratio, kSearchBeyond * ratio);
    if (!start || !step) {
        return std::nullopt;
    }
    const double outline = render::kHandleOutlineWidth * ratio;
    const double inner = *step + (kHeadNear * ratio);
    const double outer = *step + (kHeadFar * ratio);
    const std::optional<double> innerWidth = widthAt(cover, base, along, inner, ratio);
    const std::optional<double> outerWidth = widthAt(cover, base, along, outer, ratio);
    if (!innerWidth || !outerWidth) {
        return std::nullopt;
    }
    const double slope = (*outerWidth - *innerWidth) / (outer - inner);
    const double rim = outline / std::cos(std::atan(-slope * 0.5));
    const double tip = inner + ((rim - *innerWidth) / slope);
    return ArrowSize{.start = (*start + outline * 0.5) / ratio,
                     .shaft = (*step - *start) / ratio,
                     .head = (tip - *step - outline * 0.5) / ratio};
}

camera::Vec3 offset(const camera::Vec3& point, double x, double y, double z)
{
    return {point.x + x, point.y + y, point.z + z};
}

TEST_F(ViewportTest, DrawsTheHandleArrowAtItsSizeAtTwoZoomsInBothProjections)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const GridHidden gridOff(*canvas);
    const render::ViewportStyle original = canvas->viewportStyle();
    render::ViewportStyle black = original;
    black.background = {0.0F, 0.0F, 0.0F, 1.0F};
    canvas->setViewportStyle(black);
    canvas->setScene(snapshot::Scene{.bodies = {}, .origin = {}, .hidden = {}});
    const camera::Vec3 center{500.0, 400.0, 100.0};
    const camera::Vec3 eye = offset(center, 300.0, -400.0, 250.0);
    const snapshot::Color white{1.0F, 1.0F, 1.0F, 1.0F};
    const render::HandleItem arrow{.anchor = center,
                                   .toward = offset(center, 1.0, 0.6, 0.3),
                                   .colors = {.fill = white, .outline = white, .halo = white}};
    render::HandleItem facing = arrow;
    facing.toward = offset(center, (eye.x - center.x) * 0.01, (eye.y - center.y) * 0.01, (eye.z - center.z) * 0.01);
    const double ratio = canvas->viewport().pixelRatio;
    for (const camera::Projection projection : {camera::Projection::Orthographic, camera::Projection::Perspective}) {
        for (const double scale : {60.0, 240.0}) {
            const std::string label =
                std::format("{} at {}", projection == camera::Projection::Perspective ? "perspective" : "ortho", scale);
            camera::Camera camera;
            camera.setProjectionType(projection);
            ASSERT_TRUE(camera.setEyeAndCenter(eye, center));
            ASSERT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
            ASSERT_TRUE(camera.setScale(scale));
            canvas->setCamera(camera);
            canvas->setHandles({});
            const QImage plain = grabbed(*canvas);
            canvas->setHandles({arrow});
            const QImage shown = grabbed(*canvas);
            camera::Camera seen = canvas->camera();
            seen.setAspect(canvas->viewport().aspect());
            const camera::ScreenPoint from = seen.project(arrow.anchor, canvas->viewport());
            const camera::ScreenPoint to = seen.project(arrow.toward, canvas->viewport());
            const QPointF axis(to.x - from.x, to.y - from.y);
            const QPointF along = axis / std::hypot(axis.x(), axis.y());
            const std::optional<ArrowSize> size =
                measure(Cover{.plain = plain, .shown = shown}, QPointF(from.x, from.y) * ratio, along, ratio);
            ASSERT_TRUE(size.has_value()) << label;
            std::printf("%s: start %.2f shaft %.2f head %.2f px\n", label.c_str(), size->start, size->shaft,
                        size->head);
            EXPECT_NEAR(size->start, 0.0, kSizeTolerance) << label;
            EXPECT_NEAR(size->shaft, render::kHandleShaftPixels, kSizeTolerance) << label;
            EXPECT_NEAR(size->head, render::kHandleHeadPixels, kSizeTolerance) << label;
            canvas->setHandles({facing});
            EXPECT_EQ(differing(plain, grabbed(*canvas)), 0U) << label << ": arrow hides along the line of sight";
        }
    }
    canvas->setHandles({arrow});
    ui->save("handle arrow");
    canvas->setHandles({});
    canvas->setViewportStyle(original);
}

using HandlePairTest = UiTest;

double snapStepAt(double pixelsPerUnit)
{
    if (pixelsPerUnit >= 40.0) {
        return 0.1;
    }
    if (pixelsPerUnit >= 8.0) {
        return 0.5;
    }
    return pixelsPerUnit >= 1.5 ? 1.0 : 5.0;
}

struct Arrow {
    QPointF base;
    QPointF along;
    double pixelsPerUnit = 0.0;
};

Arrow arrowOf(const UiDriver& driver, const render::HandleItem& item)
{
    const gp_Pnt anchor(item.anchor.x, item.anchor.y, item.anchor.z);
    const gp_Pnt toward(item.toward.x, item.toward.y, item.toward.z);
    const QPointF base = driver.at(anchor);
    const QPointF axis = driver.at(toward) - base;
    const double pixels = std::hypot(axis.x(), axis.y());
    return {.base = base, .along = axis / pixels, .pixelsPerUnit = pixels / anchor.Distance(toward)};
}

QPointF middleOf(const Arrow& arrow)
{
    return arrow.base + arrow.along * ((render::kHandleShaftPixels + render::kHandleHeadPixels) * 0.5);
}

double distanceOf(const UiDriver& driver)
{
    const auto* box = dynamic_cast<const app::ValueEdit*>(driver.field("Distance"));
    return box != nullptr ? box->value() : std::nan("");
}

double dragBy(UiDriver& driver, const Arrow& arrow, double pixels)
{
    const QPointF grab = middleOf(arrow);
    driver.press(grab);
    for (int i = 1; i <= kDragSteps; ++i) {
        driver.move(grab + arrow.along * (pixels * i / kDragSteps), Qt::LeftButton);
    }
    driver.release(grab + arrow.along * pixels);
    return distanceOf(driver);
}

void startExtrude(UiDriver& driver)
{
    sketchRectangle(driver, core::OriginPlane::XY, gp_Pnt(-20, -10, 0), gp_Pnt(20, 10, 0));
    driver.trigger("Extrude");
    ASSERT_TRUE(driver.state()["featurePanel"]) << driver.state().dump(2);
}

TEST_F(HandlePairTest, DragsTheExtrudeArrowLikeTheOldViewAndGrabsItWithinTenPixels)
{
    if (ui->viewport() == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    UiDriver old(app::StartupOptions{.view = app::ViewKind::Old, .graphicsApi = std::nullopt});
    startExtrude(*ui);
    startExtrude(old);
    ASSERT_EQ(ui->viewport()->handles().size(), 1U);
    const render::HandleItem item = ui->viewport()->handles().front();
    for (UiDriver* driver : {ui.get(), &old}) {
        driver->wheel(arrowOf(*driver, item).base, kZoomNotches);
    }
    EXPECT_TRUE(item.active);
    EXPECT_FALSE(item.hovered || item.dragged);
    const double before = distanceOf(*ui);
    ASSERT_NEAR(distanceOf(old), before, kNear);
    const Arrow arrow = arrowOf(*ui, item);
    const Arrow reference = arrowOf(old, item);
    EXPECT_NEAR(arrow.pixelsPerUnit, reference.pixelsPerUnit, kSameScale) << "both views show the arrow at one scale";
    const double moved = dragBy(*ui, arrow, kDragPixels);
    const double expected = dragBy(old, reference, kDragPixels);
    std::printf("drag by %.0f px: viewport %.3f, old view %.3f, step %.2f at %.2f and %.2f px per mm\n", kDragPixels,
                moved, expected, snapStepAt(arrow.pixelsPerUnit), arrow.pixelsPerUnit, reference.pixelsPerUnit);
    EXPECT_GT(moved, before);
    EXPECT_NEAR(moved, expected, snapStepAt(arrow.pixelsPerUnit) + kNear);

    const Arrow dragged = arrowOf(*ui, ui->viewport()->handles().front());
    const QPointF side(-dragged.along.y(), dragged.along.x());
    const QPointF inside = middleOf(dragged) + side * kInsidePixels;
    ui->press(inside);
    EXPECT_TRUE(ui->viewport()->handles().front().dragged) << "press 9 px off the arrow grabs it";
    EXPECT_FALSE(ui->viewport()->handles().front().guide.empty()) << "grabbed arrow shows its guide";
    ui->save("extrude arrow grabbed");
    ui->release(inside);
    const QPointF outside = middleOf(dragged) + side * kOutsidePixels;
    ui->press(outside);
    EXPECT_FALSE(ui->viewport()->handles().front().dragged) << "press 11 px off the arrow misses it";
    ui->release(outside);
}

TEST_F(HandlePairTest, HoversTheArrowInBothThemes)
{
    if (ui->viewport() == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    for (const app::theme::Scheme scheme : {app::theme::Scheme::Dark, app::theme::Scheme::Light}) {
        const std::string name = scheme == app::theme::Scheme::Dark ? "dark" : "light";
        app::Preferences preferences = app::Preferences::load();
        preferences.colorScheme = scheme;
        preferences.save();
        ui = std::make_unique<UiDriver>();
        startExtrude(*ui);
        ASSERT_EQ(ui->viewport()->handles().size(), 1U) << name;
        ui->save(name + " arrow");
        ui->move(middleOf(arrowOf(*ui, ui->viewport()->handles().front())));
        EXPECT_TRUE(ui->viewport()->handles().front().hovered) << name;
        ui->save(name + " arrow hovered");
    }
}

render::HandleColors handleColors(const app::theme::Palette& palette)
{
    return {.fill = app::colorOf(palette.viewport.handle),
            .outline = app::colorOf(palette.viewport.handleOutline),
            .halo = app::colorOf(palette.viewport.halo)};
}

TEST_F(HandlePairTest, RestylesTheArrowAtOnceWhenTheThemeSwitches)
{
    if (ui->viewport() == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    app::theme::setScheme(app::theme::Scheme::Dark);
    startExtrude(*ui);
    ASSERT_EQ(ui->viewport()->handles().size(), 1U);
    EXPECT_EQ(ui->viewport()->handles().front().colors, handleColors(app::theme::kDarkPalette));
    app::theme::setScheme(app::theme::Scheme::Light);
    EXPECT_EQ(ui->viewport()->handles().front().colors, handleColors(app::theme::kLightPalette));
    app::theme::setScheme(app::theme::Scheme::Dark);
    EXPECT_EQ(ui->viewport()->handles().front().colors, handleColors(app::theme::kDarkPalette));
}

app::ValueEdit* canvasBox(UiDriver& driver)
{
    for (auto* box : driver.canvas()->window()->findChildren<app::ValueEdit*>("canvasInput")) {
        if (box->isVisible()) {
            return box;
        }
    }
    return nullptr;
}

TEST_F(HandlePairTest, FocusedBoxFollowsTheArrowWhileItIsDragged)
{
    if (ui->viewport() == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    startExtrude(*ui);
    ASSERT_EQ(ui->viewport()->handles().size(), 1U);
    app::ValueEdit* box = canvasBox(*ui);
    ASSERT_NE(box, nullptr);
    ui->click(box);
    ASSERT_EQ(ui->focused(), box);
    const double before = box->value();
    const Arrow arrow = arrowOf(*ui, ui->viewport()->handles().front());
    const QPointF grab = middleOf(arrow);
    ui->press(grab);
    for (int i = 1; i <= kDragSteps; ++i) {
        ui->move(grab + arrow.along * (kDragPixels * i / kDragSteps), Qt::LeftButton);
    }
    EXPECT_EQ(ui->focused(), box);
    const double dragged = distanceOf(*ui);
    EXPECT_GT(dragged, before);
    EXPECT_NEAR(box->value(), dragged, kNear);
    const std::optional<double> shown = app::parseValue(box->text(), core::Quantity::Length);
    ASSERT_TRUE(shown.has_value()) << box->text().toStdString();
    EXPECT_NEAR(*shown, dragged, kNear);
    EXPECT_EQ(box->selectedText(), box->text());
    ui->release(grab + arrow.along * kDragPixels);
}

} // namespace
} // namespace kalve137::uitest
