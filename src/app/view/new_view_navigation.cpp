// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/new_view_navigation.h"

#include "app/view/camera_commands.h"
#include "camera/gravity.h"
#include "render/rhi/rhi_view.h"

#include <memory>
#include <vector>

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

SnapshotNavigationScene::SnapshotNavigationScene(const render::RhiView& view, pick::SceneRays& rays)
    : m_view(&view), m_rays(&rays)
{}

std::optional<camera::Vec3> SnapshotNavigationScene::pick(const camera::Camera& camera,
                                                          const camera::Viewport& viewport, const camera::Pixel& pixel,
                                                          bool) const
{
    const std::shared_ptr<const pick::RayScene> scene = m_rays->rays(m_view->scene());
    if (scene == nullptr) {
        return std::nullopt;
    }
    const camera::Ray ray = camera.ray(pixel, viewport);
    const camera::Vec3 direction = camera::normalized(ray.direction);
    const std::optional<pick::Hit> hit =
        scene->first({ray.origin.x, ray.origin.y, ray.origin.z}, {direction.x, direction.y, direction.z});
    if (!hit) {
        return std::nullopt;
    }
    return ray.origin + direction * hit->distance;
}

camera::Vec3 SnapshotNavigationScene::gravityPoint(const camera::Camera& camera, const camera::Viewport&) const
{
    std::vector<camera::Box> boxes;
    for (const auto& body : m_view->scene().bodies) {
        const snapshot::Mesh& mesh = *body->mesh;
        if (!mesh.vertices.empty()) {
            boxes.push_back({{mesh.low[0], mesh.low[1], mesh.low[2]}, {mesh.high[0], mesh.high[1], mesh.high[2]}});
        }
    }
    return camera::gravityPoint(camera, boxes);
}

NewViewNavigation::NewViewNavigation(render::RhiView& view, pick::SceneRays& rays, CameraCommands& commands)
    : QObject(&commands), m_view(&view), m_commands(&commands), m_scene(view, rays)
{
    connect(m_view, &render::RhiView::mousePressed, this, &NewViewNavigation::press);
    connect(m_view, &render::RhiView::mouseReleased, this,
            [this](QPointF position, Qt::MouseButton, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
                release(position, buttons, modifiers);
            });
    connect(m_view, &render::RhiView::mouseMoved, this, &NewViewNavigation::move);
    connect(m_view, &render::RhiView::wheelTurned, this, &NewViewNavigation::wheel);
}

void NewViewNavigation::setNavigation(const camera::Navigation& navigation)
{
    m_navigator.setNavigation(navigation);
}

void NewViewNavigation::press(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons,
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

void NewViewNavigation::release(QPointF position, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
    if (const std::optional<camera::Pixel> pixel = pixelAt(position)) {
        m_navigator.buttons(*pixel, toButtons(buttons), toModifiers(modifiers));
        flush();
    }
}

void NewViewNavigation::move(QPointF position, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
    if (const std::optional<camera::Pixel> pixel = pixelAt(position)) {
        m_navigator.move(*pixel, toButtons(buttons), toModifiers(modifiers));
        flush();
    }
}

void NewViewNavigation::wheel(QPointF position, double notches)
{
    if (const std::optional<camera::Pixel> pixel = pixelAt(position)) {
        m_navigator.wheel(*pixel, notches);
        flush();
    }
}

std::optional<camera::Pixel> NewViewNavigation::pixelAt(QPointF position) const
{
    const std::optional<camera::Viewport> viewport = m_commands->viewport();
    if (!viewport) {
        return std::nullopt;
    }
    return camera::toPixel({position.x(), position.y()}, *viewport);
}

void NewViewNavigation::flush()
{
    if (!m_navigator.hasPending()) {
        return;
    }
    m_commands->navigate([this](camera::Camera& camera, const camera::Viewport& viewport) {
        return m_navigator.flush(camera, viewport, m_scene);
    });
}

} // namespace kalve137::app
