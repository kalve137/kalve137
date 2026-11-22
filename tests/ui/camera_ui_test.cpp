// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "camera/navigator.h"
#include "camera/views.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace kalve137::uitest {
namespace {

using CameraTest = UiTest;

constexpr double kPositionTolerance = 1e-6;
constexpr double kDirectionTolerance = 1e-9;
constexpr int kWheelStep = 120;

class OldViewScene : public camera::NavigationScene {
public:
    explicit OldViewScene(viewer::OcctViewWidget& view) : m_view(&view) {}

    std::optional<camera::Vec3> pick(const camera::Camera&, const camera::Viewport&, const camera::Pixel& pixel,
                                     bool onRay) const override
    {
        const std::optional<gp_Pnt> point = m_view->pickPoint(pixel, onRay);
        return point ? std::optional<camera::Vec3>(camera::Vec3{point->X(), point->Y(), point->Z()}) : std::nullopt;
    }

    camera::Vec3 gravityPoint(const camera::Camera&, const camera::Viewport&) const override
    {
        const gp_Pnt point = m_view->gravityPoint();
        return {point.X(), point.Y(), point.Z()};
    }

private:
    viewer::OcctViewWidget* m_view;
};

struct Step {
    enum class Kind { Press, Move, Release, Wheel };
    Kind kind = Kind::Move;
    QPointF offset;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons;
    Qt::KeyboardModifiers modifiers;
    int notches = 0;
};

unsigned toButtons(Qt::MouseButtons buttons)
{
    unsigned result = camera::NoButton;
    result |= buttons.testFlag(Qt::LeftButton) ? camera::LeftButton : 0U;
    result |= buttons.testFlag(Qt::RightButton) ? camera::RightButton : 0U;
    result |= buttons.testFlag(Qt::MiddleButton) ? camera::MiddleButton : 0U;
    return result;
}

unsigned toModifiers(Qt::KeyboardModifiers modifiers)
{
    unsigned result = camera::NoModifier;
    result |= modifiers.testFlag(Qt::ShiftModifier) ? camera::Shift : 0U;
    result |= modifiers.testFlag(Qt::ControlModifier) ? camera::Ctrl : 0U;
    result |= modifiers.testFlag(Qt::AltModifier) ? camera::Alt : 0U;
    return result;
}

std::vector<Step> drag(Qt::MouseButton button, Qt::KeyboardModifiers modifiers, QPointF from = {})
{
    std::vector<Step> steps;
    steps.push_back({Step::Kind::Press, from, button, button, modifiers});
    for (const QPointF to : {QPointF(30, 5), QPointF(70, -20), QPointF(110, -45), QPointF(90, -80)}) {
        steps.push_back({Step::Kind::Move, from + to, Qt::NoButton, button, modifiers});
    }
    steps.push_back({Step::Kind::Release, from + QPointF(90, -80), button, Qt::NoButton, modifiers});
    return steps;
}

std::vector<Step> wheel()
{
    return {{Step::Kind::Wheel, QPointF(120, -60), Qt::NoButton, {}, {}, 1},
            {Step::Kind::Wheel, QPointF(120, -60), Qt::NoButton, {}, {}, 2},
            {Step::Kind::Wheel, QPointF(-150, 40), Qt::NoButton, {}, {}, -1}};
}

void send(viewer::OcctViewWidget& view, const Step& step, QPointF pos)
{
    if (step.kind == Step::Kind::Wheel) {
        QWheelEvent event(pos, view.mapToGlobal(pos), QPoint(), QPoint(0, kWheelStep * step.notches), Qt::NoButton,
                          step.modifiers, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&view, &event);
        return;
    }
    const QEvent::Type type = step.kind == Step::Kind::Press     ? QEvent::MouseButtonPress
                              : step.kind == Step::Kind::Release ? QEvent::MouseButtonRelease
                                                                 : QEvent::MouseMove;
    QMouseEvent event(type, pos, view.mapToGlobal(pos), step.button, step.buttons, step.modifiers);
    QApplication::sendEvent(&view, &event);
}

void feed(camera::Navigator& navigator, const Step& step, const camera::Pixel& pixel)
{
    switch (step.kind) {
    case Step::Kind::Press:
    case Step::Kind::Release:
        navigator.buttons(pixel, toButtons(step.buttons), toModifiers(step.modifiers));
        return;
    case Step::Kind::Move:
        navigator.move(pixel, toButtons(step.buttons), toModifiers(step.modifiers));
        return;
    case Step::Kind::Wheel:
        navigator.wheel(pixel, step.notches);
        return;
    }
}

void expectClose(const camera::Camera& ours, const camera::Camera& theirs, const std::string& where)
{
    const double size = std::max({1.0, camera::length(theirs.eye()), theirs.distance()});
    const auto within = [&](const camera::Vec3& a, const camera::Vec3& b, double tolerance, const char* what) {
        EXPECT_LT(camera::distance(a, b), tolerance) << where << " " << what;
    };
    within(ours.eye(), theirs.eye(), kPositionTolerance * size, "eye");
    within(ours.center(), theirs.center(), kPositionTolerance * size, "center");
    within(ours.direction(), theirs.direction(), kDirectionTolerance * 1000.0, "direction");
    within(ours.up(), theirs.up(), kDirectionTolerance * 1000.0, "up");
    EXPECT_NEAR(ours.scale(), theirs.scale(), kPositionTolerance * std::max(1.0, theirs.scale())) << where << " scale";
    EXPECT_EQ(ours.isOrthographic(), theirs.isOrthographic()) << where;
}

void expectSameMoves(UiDriver& ui, const camera::Navigation& navigation, bool perspective,
                     const std::vector<Step>& steps, const std::string& name)
{
    viewer::OcctViewWidget& view = ui.view();
    view.setNavigation(navigation);
    view.setPerspective(perspective);
    view.homeView();
    view.fitAll();
    ui.settle();

    const std::optional<camera::Viewport> viewport = view.viewport();
    ASSERT_TRUE(viewport) << name;
    camera::Camera ours = view.currentCamera();
    camera::Navigator navigator;
    navigator.setNavigation(navigation);
    const OldViewScene scene(view);
    const QPointF middle = ui.centerOf(&view);

    const camera::Camera start = ours;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const Step& step = steps[i];
        const QPointF pos = middle + step.offset;
        feed(navigator, step, camera::toPixel({pos.x(), pos.y()}, *viewport));
        navigator.flush(ours, *viewport, scene);
        send(view, step, pos);
        ui.frame();
        expectClose(ours, view.currentCamera(), name + " step " + std::to_string(i));
    }
    const camera::Camera end = view.currentCamera();
    const double moved = camera::distance(end.eye(), start.eye()) / start.scale() +
                         camera::distance(end.direction(), start.direction()) +
                         std::abs(end.scale() / start.scale() - 1.0);
    EXPECT_GT(moved, 0.01) << name << " never moved the old view";
}

camera::Navigation preset(camera::NavigationPreset value)
{
    camera::Navigation navigation;
    navigation.preset = value;
    return navigation;
}

TEST_F(CameraTest, NavigatorMovesLikeTheOldViewForEachGesture)
{
    ui->trigger("Open Sample");
    ui->settle();
    for (const bool perspective : {false, true}) {
        const std::string projection = perspective ? " perspective" : " orthographic";
        const camera::Navigation plain;
        expectSameMoves(*ui, plain, perspective, drag(Qt::MiddleButton, Qt::NoModifier), "orbit" + projection);
        expectSameMoves(*ui, plain, perspective, drag(Qt::MiddleButton, Qt::ControlModifier), "pan" + projection);
        expectSameMoves(*ui, plain, perspective, drag(Qt::MiddleButton, Qt::ShiftModifier), "drag zoom" + projection);
        expectSameMoves(*ui, plain, perspective, wheel(), "wheel zoom to cursor" + projection);

        camera::Navigation reverse;
        reverse.reverseZoom = true;
        expectSameMoves(*ui, reverse, perspective, wheel(), "reverse zoom" + projection);

        camera::Navigation centered;
        centered.zoomToCursor = false;
        expectSameMoves(*ui, centered, perspective, wheel(), "wheel zoom at the middle" + projection);

        camera::Navigation constrained;
        constrained.constrainedOrbit = true;
        expectSameMoves(*ui, constrained, perspective, drag(Qt::MiddleButton, Qt::NoModifier),
                        "constrained orbit" + projection);

        camera::Navigation aroundCursor;
        aroundCursor.orbitAroundCursor = true;
        expectSameMoves(*ui, aroundCursor, perspective, drag(Qt::MiddleButton, Qt::NoModifier),
                        "orbit around the cursor" + projection);
    }
}

TEST_F(CameraTest, NavigatorFollowsEachPresetLikeTheOldView)
{
    ui->trigger("Open Sample");
    ui->settle();
    struct Binding {
        camera::NavigationPreset preset;
        Qt::MouseButton button;
        Qt::KeyboardModifiers orbit;
        Qt::KeyboardModifiers pan;
        Qt::KeyboardModifiers zoom;
    };
    const std::vector<Binding> bindings{
        {camera::NavigationPreset::MiddleOrbitsCtrlPans, Qt::MiddleButton, Qt::NoModifier, Qt::ControlModifier,
         Qt::ShiftModifier},
        {camera::NavigationPreset::MiddlePansShiftOrbits, Qt::MiddleButton, Qt::ShiftModifier, Qt::NoModifier,
         Qt::ControlModifier},
        {camera::NavigationPreset::MiddleOrbitsShiftPans, Qt::MiddleButton, Qt::NoModifier, Qt::ShiftModifier,
         Qt::ControlModifier},
        {camera::NavigationPreset::RightOrbitsCtrlPans, Qt::RightButton, Qt::NoModifier, Qt::ControlModifier,
         Qt::ShiftModifier},
    };
    for (const Binding& binding : bindings) {
        const camera::Navigation navigation = preset(binding.preset);
        const std::string name = "preset " + std::to_string(static_cast<int>(binding.preset));
        std::vector<Step> steps = drag(binding.button, binding.orbit);
        for (const Step& step : drag(binding.button, binding.pan, QPointF(-60, 30))) {
            steps.push_back(step);
        }
        for (const Step& step : drag(binding.button, binding.zoom, QPointF(40, 40))) {
            steps.push_back(step);
        }
        expectSameMoves(*ui, navigation, true, steps, name);
    }
}

TEST_F(CameraTest, AModifierChangeMidDragSwitchesGestureLikeTheOldView)
{
    ui->trigger("Open Sample");
    ui->settle();
    std::vector<Step> steps = drag(Qt::MiddleButton, Qt::NoModifier);
    steps.insert(steps.end() - 1,
                 {{Step::Kind::Move, QPointF(60, -90), Qt::NoButton, Qt::MiddleButton, Qt::ControlModifier},
                  {Step::Kind::Move, QPointF(20, -60), Qt::NoButton, Qt::MiddleButton, Qt::ControlModifier}});
    for (const bool perspective : {false, true}) {
        expectSameMoves(*ui, camera::Navigation{}, perspective, steps, perspective ? "perspective" : "orthographic");
    }
}

TEST_F(CameraTest, ViewTargetsLandWhereTheOldViewLands)
{
    ui->trigger("Open Sample");
    ui->settle();
    viewer::OcctViewWidget& view = ui->view();
    view.setNavigation(camera::Navigation{});
    for (const bool perspective : {false, true}) {
        view.setPerspective(perspective);
        view.homeView();
        ui->settle();
        for (const camera::StandardView standard :
             {camera::StandardView::Front, camera::StandardView::Back, camera::StandardView::Left,
              camera::StandardView::Right, camera::StandardView::Top, camera::StandardView::Bottom}) {
            camera::Camera ours = view.currentCamera();
            camera::standardView(ours, standard);
            view.standardView(standard);
            expectClose(ours, view.currentCamera(), "standard view " + std::to_string(static_cast<int>(standard)));
            ASSERT_TRUE(camera::fit(ours, view.sceneBounds()));
            view.fitAll();
            expectClose(ours, view.currentCamera(), "fit " + std::to_string(static_cast<int>(standard)));
        }
        camera::Camera ours = view.currentCamera();
        camera::homeView(ours);
        view.homeView();
        expectClose(ours, view.currentCamera(), "home");
    }
}

} // namespace
} // namespace kalve137::uitest
