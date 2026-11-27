// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/viewport_navigation.h"

#include "app/view/camera_commands.h"
#include "camera/gravity.h"
#include "render/draw_list.h"
#include "render/rhi/viewport.h"

#include <memory>

namespace kalve137::app {

namespace {

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

} // namespace

SnapshotNavigationScene::SnapshotNavigationScene(render::Viewport& view) : m_view(&view) {}

std::optional<camera::Vec3> SnapshotNavigationScene::pick(const camera::Camera& camera,
                                                          const camera::Viewport& viewport, const camera::Pixel& pixel,
                                                          bool onRay) const
{
    const std::optional<camera::Vec3> surface = m_view->surfaceAt(pixel.x, pixel.y);
    if (!surface || !onRay) {
        return surface;
    }
    const camera::Ray ray = camera.ray(pixel, viewport);
    const camera::Vec3 direction = camera::normalized(ray.direction);
    return ray.origin + direction * camera::dot(*surface - ray.origin, direction);
}

camera::Vec3 SnapshotNavigationScene::gravityPoint(const camera::Camera& camera, const camera::Viewport&) const
{
    return camera::gravityPoint(camera, render::bodyBounds(m_view->scene()));
}

ViewportNavigation::ViewportNavigation(render::Viewport& view, CameraCommands& commands)
    : QObject(&commands), m_view(&view), m_commands(&commands), m_scene(view)
{
    connect(m_view, &render::Viewport::mousePressed, this, &ViewportNavigation::press);
    connect(m_view, &render::Viewport::mouseReleased, this,
            [this](QPointF position, Qt::MouseButton, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
                release(position, buttons, modifiers);
            });
    connect(m_view, &render::Viewport::mouseMoved, this, &ViewportNavigation::move);
    connect(m_view, &render::Viewport::wheelTurned, this, &ViewportNavigation::wheel);
}

void ViewportNavigation::setNavigation(const camera::Navigation& navigation)
{
    m_navigator.setNavigation(navigation);
}

void ViewportNavigation::press(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons,
                               Qt::KeyboardModifiers modifiers)
{
    const Qt::MouseButton menuButton = m_navigator.navigation().preset == camera::NavigationPreset::RightOrbitsCtrlPans
                                           ? Qt::MiddleButton
                                           : Qt::RightButton;
    if (button == menuButton) {
        emit menuRequested(m_view->mapToGlobal(position).toPoint());
        return;
    }
    if (const std::optional<camera::Pixel> pixel = pixelAt(position)) {
        m_navigator.buttons(*pixel, toButtons(buttons), toModifiers(modifiers));
        flush();
    }
}

void ViewportNavigation::release(QPointF position, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
    if (const std::optional<camera::Pixel> pixel = pixelAt(position)) {
        m_navigator.buttons(*pixel, toButtons(buttons), toModifiers(modifiers));
        flush();
    }
}

void ViewportNavigation::move(QPointF position, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
    if (const std::optional<camera::Pixel> pixel = pixelAt(position)) {
        m_navigator.move(*pixel, toButtons(buttons), toModifiers(modifiers));
        flush();
    }
}

void ViewportNavigation::wheel(QPointF position, double notches)
{
    if (const std::optional<camera::Pixel> pixel = pixelAt(position)) {
        m_navigator.wheel(*pixel, notches);
        flush();
    }
}

std::optional<camera::Pixel> ViewportNavigation::pixelAt(QPointF position) const
{
    const std::optional<camera::Viewport> viewport = m_commands->viewport();
    if (!viewport) {
        return std::nullopt;
    }
    return camera::toPixel({position.x(), position.y()}, *viewport);
}

void ViewportNavigation::flush()
{
    if (!m_navigator.hasPending()) {
        return;
    }
    m_commands->navigate([this](camera::Camera& camera, const camera::Viewport& viewport) {
        return m_navigator.flush(camera, viewport, m_scene);
    });
}

} // namespace kalve137::app
