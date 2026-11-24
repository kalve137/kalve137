// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/camera_commands.h"

#include "app/view/viewport_navigation.h"
#include "camera/views.h"
#include "render/rhi/viewport.h"
#include "viewer/occt_view_widget.h"

#include <QEasingCurve>
#include <QVariantAnimation>

#include <gp_Ax3.hxx>

#include <cmath>
#include <utility>

namespace kalve137::app {

namespace {

constexpr double kMillisecondsPerSecond = 1000.0;

camera::Vec3 toVec(const gp_Dir& direction)
{
    return {direction.X(), direction.Y(), direction.Z()};
}

} // namespace

CameraCommands::CameraCommands(viewer::OcctViewWidget& view, render::Viewport* newView,
                               std::function<bool()> sceneIsEmpty, QObject* parent)
    : QObject(parent), m_view(&view), m_viewport(newView), m_sceneIsEmpty(std::move(sceneIsEmpty))
{
    if (m_viewport == nullptr) {
        return;
    }
    m_camera = m_view->currentCamera();
    m_animation = new QVariantAnimation(this);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    m_animation->setEasingCurve(QEasingCurve::Linear);
    connect(m_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { show(m_transition.at(value.toDouble() * m_transition.seconds())); });
    m_navigation = new ViewportNavigation(*m_viewport, *this);
    connect(m_navigation, &ViewportNavigation::menuRequested, this, &CameraCommands::menuRequested);
    connect(m_viewport, &render::Viewport::resized, this, &CameraCommands::syncViewport);
}

void CameraCommands::home(Move move)
{
    if (m_viewport == nullptr) {
        const auto homeAndFit = [this] {
            m_view->homeView();
            fitOldView();
        };
        if (move == Move::Animate) {
            m_view->animateCamera(homeAndFit);
        } else {
            homeAndFit();
        }
        return;
    }
    change(move, [this](camera::Camera& camera) {
        camera::homeView(camera);
        fitTo(camera);
    });
}

void CameraCommands::standardView(camera::StandardView view)
{
    if (m_viewport == nullptr) {
        m_view->animateCamera([this, view] {
            m_view->standardView(view);
            fitOldView();
        });
        return;
    }
    change(Move::Animate, [this, view](camera::Camera& camera) {
        camera::standardView(camera, view);
        fitTo(camera);
    });
}

void CameraCommands::fit(Move move)
{
    if (m_viewport == nullptr) {
        if (move == Move::Animate) {
            m_view->animateCamera([this] { fitOldView(); });
        } else {
            fitOldView();
        }
        return;
    }
    change(move, [this](camera::Camera& camera) { fitTo(camera); });
}

void CameraCommands::lookAt(const gp_Ax3& plane)
{
    if (m_viewport == nullptr) {
        m_view->animateCamera([this, &plane] {
            m_view->lookAt(plane);
            fitOldView();
        });
        return;
    }
    change(Move::Animate, [this, &plane](camera::Camera& camera) {
        camera::lookAt(camera, toVec(plane.Direction()), toVec(plane.YDirection()));
        fitTo(camera);
    });
}

void CameraCommands::saveView()
{
    if (m_viewport == nullptr) {
        m_view->saveView();
        return;
    }
    if (isReady()) {
        m_savedView = isAnimating() ? m_transition.end() : m_camera;
    }
}

bool CameraCommands::hasSavedView() const
{
    return m_viewport == nullptr ? m_view->hasSavedView() : m_savedView.has_value();
}

void CameraCommands::restoreSavedView()
{
    if (m_viewport == nullptr) {
        m_view->restoreSavedView();
        return;
    }
    const std::optional<camera::Camera> saved = std::exchange(m_savedView, std::nullopt);
    if (!saved || !isReady() || !saved->isUsable()) {
        return;
    }
    change(Move::Animate, [&saved](camera::Camera& camera) {
        const double aspect = camera.aspect();
        camera = *saved;
        camera.setAspect(aspect);
    });
}

void CameraCommands::dropSavedView()
{
    m_view->dropSavedView();
    m_savedView.reset();
}

void CameraCommands::setPerspective(bool perspective)
{
    const bool changed = isPerspective() != perspective;
    if (m_viewport == nullptr) {
        m_view->setPerspective(perspective);
        if (changed && m_view->isReady()) {
            fitOldView();
        }
        return;
    }
    change(Move::Jump, [perspective](camera::Camera& camera) {
        camera.setProjectionType(perspective ? camera::Projection::Perspective : camera::Projection::Orthographic);
    });
    if (changed && isReady()) {
        fit(Move::Jump);
    }
}

bool CameraCommands::isPerspective() const
{
    return m_viewport == nullptr ? m_view->isPerspective() : !m_camera.isOrthographic();
}

void CameraCommands::setNavigation(const camera::Navigation& navigation)
{
    m_navigationSettings = navigation;
    m_view->setNavigation(navigation);
    m_animateTransitions = navigation.animateTransitions;
    if (m_navigation != nullptr) {
        m_navigation->setNavigation(navigation);
    }
}

bool CameraCommands::isAnimating() const
{
    if (m_viewport == nullptr) {
        return m_view->isAnimating();
    }
    return m_animation->state() == QAbstractAnimation::Running;
}

std::optional<camera::Viewport> CameraCommands::viewport() const
{
    if (m_viewport == nullptr) {
        return m_view->viewport();
    }
    const camera::Viewport viewport = m_viewport->viewport();
    return viewport.isValid() ? std::optional(viewport) : std::nullopt;
}

void CameraCommands::syncViewport()
{
    if (m_viewport != nullptr) {
        show(m_camera);
    }
}

void CameraCommands::navigate(const std::function<bool(camera::Camera&, const camera::Viewport&)>& step)
{
    const std::optional<camera::Viewport> size = viewport();
    if (m_viewport == nullptr || !size) {
        return;
    }
    camera::Camera next = m_camera;
    if (!step(next, *size)) {
        return;
    }
    m_animation->stop();
    show(next);
}

void CameraCommands::change(Move move, const std::function<void(camera::Camera&)>& edit)
{
    const camera::Camera start = m_camera;
    m_animation->stop();
    camera::Camera end = start;
    if (const std::optional<camera::Viewport> size = viewport()) {
        end.setAspect(size->aspect());
    }
    edit(end);
    if (!end.isUsable()) {
        return;
    }
    if (move == Move::Jump || !isReady() || !m_animateTransitions) {
        show(end);
        return;
    }
    m_transition = camera::Transition(start, end);
    m_animation->setDuration(static_cast<int>(std::lround(m_transition.seconds() * kMillisecondsPerSecond)));
    m_animation->start();
}

void CameraCommands::fitTo(camera::Camera& camera)
{
    const std::optional<camera::Box> bounds = m_viewport->fitBounds(kEmptySceneHalfSize);
    if (!bounds) {
        camera::fitAround(camera, kEmptySceneHalfSize);
    } else {
        camera::fit(camera, bounds);
    }
}

void CameraCommands::fitOldView()
{
    if (m_sceneIsEmpty()) {
        m_view->fitAround(kEmptySceneHalfSize);
    } else {
        m_view->fitAll();
    }
}

void CameraCommands::show(camera::Camera camera)
{
    const camera::Viewport size = m_viewport->viewport();
    if (size.isValid()) {
        camera.setAspect(size.aspect());
    }
    camera.fitDepth(sceneBounds());
    m_camera = camera;
    m_view->setSyncedCamera(m_camera, size);
    m_viewport->setCamera(m_camera);
}

const std::optional<camera::Box>& CameraCommands::sceneBounds() const
{
    return m_viewport->sceneBounds();
}

bool CameraCommands::isReady() const
{
    return m_viewport == nullptr ? m_view->isReady() : m_viewport->isReady();
}

} // namespace kalve137::app
