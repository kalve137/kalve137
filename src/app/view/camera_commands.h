// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "camera/navigation.h"
#include "camera/transition.h"

#include <QObject>
#include <QPoint>

#include <gp_Ax3.hxx>

#include <functional>
#include <optional>

class QVariantAnimation;

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::render {
class Viewport;
}

namespace kalve137::app {

class ViewportNavigation;

class CameraCommands : public QObject {
    Q_OBJECT

public:
    enum class Move { Animate, Jump };

    static constexpr double kEmptySceneHalfSize = 60.0;

    CameraCommands(viewer::OcctViewWidget& view, render::Viewport* newView, std::function<bool()> sceneIsEmpty,
                   QObject* parent);

    void home(Move move);
    void standardView(camera::StandardView view);
    void fit(Move move);
    void lookAt(const gp_Ax3& plane);
    void saveView();
    bool hasSavedView() const;
    void restoreSavedView();
    void dropSavedView();
    void setPerspective(bool perspective);
    bool isPerspective() const;
    void setNavigation(const camera::Navigation& navigation);
    const camera::Navigation& navigation() const { return m_navigationSettings; }

    bool isAnimating() const;
    const camera::Camera& camera() const { return m_camera; }
    std::optional<camera::Viewport> viewport() const;
    void syncViewport();
    void navigate(const std::function<bool(camera::Camera&, const camera::Viewport&)>& step);

signals:
    void menuRequested(QPoint globalPosition);

private:
    void change(Move move, const std::function<void(camera::Camera&)>& edit);
    void fitTo(camera::Camera& camera);
    void fitOldView();
    void show(camera::Camera camera);
    const std::optional<camera::Box>& sceneBounds() const;
    bool isReady() const;

    viewer::OcctViewWidget* m_view;
    render::Viewport* m_viewport;
    std::function<bool()> m_sceneIsEmpty;
    ViewportNavigation* m_navigation = nullptr;
    camera::Navigation m_navigationSettings;
    QVariantAnimation* m_animation = nullptr;
    bool m_animateTransitions = true;
    camera::Camera m_camera;
    camera::Transition m_transition;
    std::optional<camera::Camera> m_savedView;
};

} // namespace kalve137::app
