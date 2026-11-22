// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-FileCopyrightText: 2016-2019 OPEN CASCADE SAS
// SPDX-License-Identifier: GPL-3.0-or-later AND LGPL-2.1-only WITH OCCT-exception-1.0

#include "camera/navigator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace kalve137::camera {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kScrollZoomRatio = 15.0;
constexpr double kMinCameraDistance = 1.0;
constexpr double kOrbitAcceleration = 1.0;
constexpr double kMinZoomSize = 1e-7;
constexpr double kMaxZoomSize = 1e12;
constexpr double kAngular = 1e-12;
constexpr double kPitchMargin = kPi / 180.0;
constexpr double kResolution = std::numeric_limits<double>::min();

double clampZoom(double coefficient, const Vec3& size)
{
    double clamped = coefficient;
    for (const double extent : {size.x, size.y}) {
        if (extent < clamped * kMinZoomSize) {
            clamped = extent / kMinZoomSize;
        } else if (extent > clamped * kMaxZoomSize) {
            clamped = extent / kMaxZoomSize;
        }
    }
    return clamped;
}

double zoomCoefficient(double delta)
{
    const double coefficient = std::abs(delta) / 100.0 + 1.0;
    return delta > 0.0 ? coefficient : 1.0 / coefficient;
}

double wrapTurn(double radians)
{
    constexpr double kTurn = 2.0 * kPi;
    while (radians > kTurn) {
        radians -= kTurn;
    }
    while (radians < -kTurn) {
        radians += kTurn;
    }
    return radians;
}

Pixel middleOf(const Viewport& viewport)
{
    return {viewport.width / 2, viewport.height / 2};
}

} // namespace

std::optional<Gesture> gestureFor(NavigationPreset preset, unsigned buttons, unsigned modifiers)
{
    unsigned button = MiddleButton;
    Gesture plain = Gesture::Orbit;
    Gesture withShift = Gesture::Zoom;
    Gesture withCtrl = Gesture::Pan;
    switch (preset) {
    case NavigationPreset::MiddleOrbitsCtrlPans:
        break;
    case NavigationPreset::MiddlePansShiftOrbits:
        plain = Gesture::Pan;
        withShift = Gesture::Orbit;
        withCtrl = Gesture::Zoom;
        break;
    case NavigationPreset::MiddleOrbitsShiftPans:
        withShift = Gesture::Pan;
        withCtrl = Gesture::Zoom;
        break;
    case NavigationPreset::RightOrbitsCtrlPans:
        button = RightButton;
        break;
    }
    if (buttons != button) {
        return std::nullopt;
    }
    switch (modifiers) {
    case NoModifier:
        return plain;
    case Shift:
        return withShift;
    case Ctrl:
        return withCtrl;
    default:
        return std::nullopt;
    }
}

void Navigator::setNavigation(const Navigation& navigation)
{
    m_navigation = navigation;
}

void Navigator::buttons(const Pixel& pixel, unsigned buttons, unsigned modifiers)
{
    m_modifiers = modifiers;
    m_pressed = buttons;
    if (buttons != NoButton) {
        m_pressPoint = pixel;
        m_progressPoint = pixel;
    }
    m_gesture = gestureFor(m_navigation.preset, buttons, modifiers);
    if (m_gesture == Gesture::Orbit) {
        m_startOrbit = true;
    } else if (m_gesture == Gesture::Pan) {
        m_startPan = true;
    }
}

void Navigator::move(const Pixel& pixel, unsigned buttons, unsigned modifiers)
{
    const Pixel delta{pixel.x - m_progressPoint.x, pixel.y - m_progressPoint.y};
    if (m_modifiers != modifiers) {
        this->buttons(pixel, buttons, modifiers);
    }
    if (!m_gesture) {
        return;
    }
    const bool moved = std::abs(delta.x) + std::abs(delta.y) > 0;
    switch (*m_gesture) {
    case Gesture::Orbit:
        if (m_startOrbit) {
            m_pending.orbitStart = true;
            m_pending.orbitFrom = {static_cast<double>(m_pressPoint.x), static_cast<double>(m_pressPoint.y)};
            m_startOrbit = false;
        }
        if (moved) {
            m_pending.orbitRotate = true;
            m_pending.orbitTo = {m_pressPoint.x + (pixel.x - m_pressPoint.x) * kOrbitAcceleration,
                                 m_pressPoint.y + (pixel.y - m_pressPoint.y) * kOrbitAcceleration};
            m_progressPoint = pixel;
        }
        return;
    case Gesture::Zoom:
        if (delta.x != 0) {
            addZoom({{-1, -1}, static_cast<double>(delta.x)});
            m_progressPoint = pixel;
        }
        return;
    case Gesture::Pan:
        if (moved) {
            if (m_startPan) {
                m_pending.panStart = true;
                m_pending.panFrom = m_pressPoint;
                m_startPan = false;
            }
            const Pixel step{delta.x, -delta.y};
            if (m_pending.pan) {
                m_pending.panDelta = {m_pending.panDelta.x + step.x, m_pending.panDelta.y + step.y};
            } else {
                m_pending.pan = true;
                m_pending.panDelta = step;
            }
            m_progressPoint = pixel;
        }
        return;
    }
}

void Navigator::wheel(const Pixel& pixel, double notches)
{
    if (!std::isfinite(notches) || notches == 0.0) {
        return;
    }
    Zoom zoom;
    zoom.delta = notches * (m_navigation.reverseZoom ? -1.0 : 1.0) * kScrollZoomRatio;
    if (m_navigation.zoomToCursor) {
        zoom.pixel = pixel;
    }
    addZoom(zoom);
}

void Navigator::reset()
{
    m_pending = {};
    m_gesture.reset();
    m_pressed = NoButton;
    m_modifiers = NoModifier;
    m_startOrbit = false;
    m_startPan = false;
}

bool Navigator::hasPending() const
{
    return m_pending.orbitStart || m_pending.orbitRotate || m_pending.panStart || m_pending.pan ||
           !m_pending.zooms.empty();
}

void Navigator::addZoom(const Zoom& zoom)
{
    if (!m_pending.zooms.empty() && m_pending.zooms.back().pixel == zoom.pixel) {
        m_pending.zooms.back().delta += zoom.delta;
        return;
    }
    m_pending.zooms.push_back(zoom);
}

bool Navigator::flush(Camera& camera, const Viewport& viewport, const NavigationScene& scene)
{
    const Pending input = std::exchange(m_pending, {});
    if (!viewport.isValid() || !camera.isUsable()) {
        return false;
    }
    const bool any = input.orbitStart || input.orbitRotate || input.panStart || input.pan || !input.zooms.empty();
    if (!any) {
        return false;
    }
    const Camera before = camera;
    camera.repairUp();
    camera.setAspect(viewport.aspect());

    if (input.panStart) {
        m_panAnchor.reset();
        if (!camera.isOrthographic()) {
            if (input.panFrom.x >= 0 && input.panFrom.y >= 0) {
                m_panAnchor = scene.pick(camera, viewport, input.panFrom, false);
            }
            if (!m_panAnchor) {
                m_panAnchor = scene.pick(camera, viewport, middleOf(viewport), false);
            }
        }
    }
    if (input.pan) {
        panBy(camera, viewport, input.panDelta);
    }
    if (input.orbitStart) {
        std::optional<Vec3> pivot;
        if (m_navigation.orbitAroundCursor) {
            const Pixel from{static_cast<int>(input.orbitFrom.x), static_cast<int>(input.orbitFrom.y)};
            pivot = scene.pick(camera, viewport, from, true);
        }
        m_pivot = pivot ? *pivot : scene.gravityPoint(camera, viewport);
        m_orbitStart = camera;
        m_orbitFrom = input.orbitFrom;
    }
    if (input.orbitRotate) {
        orbit(camera, viewport, input.orbitTo);
    }
    for (Zoom step : input.zooms) {
        if (!camera.isOrthographic()) {
            if (step.hasPixel()) {
                if (const std::optional<Vec3> point = scene.pick(camera, viewport, step.pixel, true)) {
                    zoom(camera, viewport, step, point);
                    continue;
                }
            }
            if (const std::optional<Vec3> point = scene.pick(camera, viewport, middleOf(viewport), true)) {
                step.pixel = {-1, -1};
                zoom(camera, viewport, step, point);
                continue;
            }
        }
        zoom(camera, viewport, step, std::nullopt);
    }

    if (!camera.isUsable()) {
        camera = before;
        return false;
    }
    return true;
}

void Navigator::panBy(Camera& camera, const Viewport& viewport, const Pixel& delta) const
{
    if (camera.isOrthographic() || !m_panAnchor) {
        const Vec3 size = camera.viewDimensions();
        camera.translateInView(-size.x * delta.x / viewport.width, -size.x * delta.y / viewport.width);
        camera.scaleTo(size.x, size.y);
        return;
    }
    const Vec3 size = camera.viewDimensions(dot(*m_panAnchor - camera.eye(), camera.direction()));
    camera.translateInView(-size.x * delta.x / viewport.width, -size.x * delta.y / viewport.width);
}

void Navigator::orbit(Camera& camera, const Viewport& viewport, const Point& to) const
{
    const Vec3 startUp = m_orbitStart.up();
    const Vec3 startDirection = m_orbitStart.direction();
    const Vec3 startEye = m_orbitStart.eye();
    const Vec3 startCenter = m_orbitStart.center();

    if (m_navigation.constrainedOrbit) {
        const double yaw = (m_orbitFrom.x - to.x) / viewport.width * (kPi * 0.5);
        double pitch = (to.y - m_orbitFrom.y) / viewport.height * (kPi * 0.5);
        if (std::abs(pitch) > kResolution) {
            const double current = std::asin(std::clamp(-startDirection.z, -1.0, 1.0));
            const double limited = std::clamp(current + pitch, -kPi * 0.5 + kPitchMargin, kPi * 0.5 - kPitchMargin);
            pitch = limited - current;
        }
        const Vec3 pitchAxis = cross(startUp, startDirection);
        const bool hasYaw = std::abs(yaw) > kAngular;
        const bool hasPitch = std::abs(pitch) > kAngular && length(pitchAxis) > kResolution;
        if (hasYaw || hasPitch) {
            Motion turn;
            if (hasYaw) {
                turn = turn * Motion::rotationAbout(m_pivot, {0.0, 0.0, 1.0}, yaw);
            }
            if (hasPitch) {
                turn = turn * Motion::rotationAbout(m_pivot, pitchAxis, pitch);
            }
            camera.setUp(turn.direction(startUp));
            camera.setEyeAndCenter(m_pivot + turn.direction(startEye - m_pivot),
                                   m_pivot + turn.direction(startCenter - m_pivot));
        } else {
            camera.setUp(startUp);
            camera.setEyeAndCenter(startEye, startCenter);
        }
        camera.orthogonalizeUp();
        return;
    }

    camera.setEyeAndCenter(startEye, startCenter);
    camera.setUp(startUp);
    camera.setDirectionFromEye(startDirection);
    const Vec3 size = camera.viewDimensions();
    const double rx = static_cast<int>(viewport.width * size.x / size.x);
    const double ry = static_cast<int>(viewport.width * size.y / size.x);
    const double dx = wrapTurn((to.x - m_orbitFrom.x) * kPi / rx);
    const double dy = wrapTurn((m_orbitFrom.y - to.y) * kPi / ry);
    const Vec3 back = -camera.direction();
    const Vec3 up = camera.up();
    const Vec3 side = normalized(cross(up, back));
    camera.transform(Motion::rotationAbout(m_pivot, up, -dx) * Motion::rotationAbout(m_pivot, side, dy));
}

void Navigator::zoom(Camera& camera, const Viewport& viewport, const Zoom& step, const std::optional<Vec3>& point) const
{
    if (point) {
        const double reach = std::max(kMinCameraDistance, distance(*point, camera.eye()));
        camera.setCenter(camera.eye() + camera.direction() * reach);
    }
    const double wanted = zoomCoefficient(step.delta);
    if (!step.hasPixel()) {
        camera.setScale(camera.scale() / clampZoom(wanted, camera.viewDimensions()));
        return;
    }
    const double coefficient = clampZoom(wanted, camera.viewDimensions());
    const double width = viewport.width;
    const double height = viewport.height;
    const Vec3 atCursor =
        camera.projectionToView({step.pixel.x * 2.0 / width - 1.0, (height - step.pixel.y) * 2.0 / height - 1.0, 0.0});
    camera.setScale(camera.scale() / coefficient);

    const Vec3 side = normalized(cross(camera.direction(), camera.up()));
    const Vec3 up = cross(-camera.direction(), side);
    double dx = atCursor.x - atCursor.x / coefficient;
    double dy = atCursor.y - atCursor.y / coefficient;
    if (point) {
        const Vec3 eyeToPoint = *point - camera.eye();
        dx = dot(eyeToPoint, side);
        dy = dot(eyeToPoint, up);
        const Vec3 size = camera.viewDimensions(dot(eyeToPoint, camera.direction()));
        dx += -size.x * (step.pixel.x - 0.5 * width) / width;
        dy += -size.y * (height - step.pixel.y - 1.0 - 0.5 * height) / height;
    }
    camera.transform(Motion::translationBy(side * dx + up * dy));
}

} // namespace kalve137::camera
