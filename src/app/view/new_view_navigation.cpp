// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/new_view_navigation.h"

#include "app/view/camera_commands.h"
#include "render/rhi/rhi_view.h"
#include "viewer/occt_view_widget.h"

#include <gp_Pnt.hxx>

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

SnapshotNavigationScene::SnapshotNavigationScene(const render::RhiView& view, const viewer::OcctViewWidget& hiddenView)
    : m_view(&view), m_hiddenView(&hiddenView)
{}

std::optional<camera::Vec3> SnapshotNavigationScene::pick(const camera::Camera& camera,
                                                          const camera::Viewport& viewport, const camera::Pixel& pixel,
                                                          bool) const
{
    const pick::RayScene* scene = rays();
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

camera::Vec3 SnapshotNavigationScene::gravityPoint(const camera::Camera&, const camera::Viewport&) const
{
    const gp_Pnt point = m_hiddenView->gravityPoint();
    return {point.X(), point.Y(), point.Z()};
}

const pick::RayScene* SnapshotNavigationScene::rays() const
{
    const std::vector<std::shared_ptr<const snapshot::Body>>& bodies = m_view->scene().bodies;
    if (bodies.empty()) {
        return nullptr;
    }
    if (m_rays == nullptr || bodies != m_bodies) {
        std::vector<const snapshot::Mesh*> meshes;
        meshes.reserve(bodies.size());
        for (const std::shared_ptr<const snapshot::Body>& body : bodies) {
            meshes.push_back(body->mesh.get());
        }
        m_rays = std::make_unique<pick::RayScene>(meshes);
        m_bodies = bodies;
    }
    return m_rays.get();
}

NewViewNavigation::NewViewNavigation(render::RhiView& view, const viewer::OcctViewWidget& hiddenView,
                                     CameraCommands& commands)
    : QObject(&commands), m_view(&view), m_commands(&commands), m_scene(view, hiddenView)
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
