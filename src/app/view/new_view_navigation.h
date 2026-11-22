// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/navigator.h"
#include "pick/ray_scene.h"
#include "snapshot/body.h"

#include <QObject>
#include <QPoint>
#include <QPointF>

#include <memory>
#include <optional>
#include <vector>

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::render {
class RhiView;
}

namespace kalve137::app {

class CameraCommands;

class SnapshotNavigationScene : public camera::NavigationScene {
public:
    SnapshotNavigationScene(const render::RhiView& view, const viewer::OcctViewWidget& hiddenView);

    std::optional<camera::Vec3> pick(const camera::Camera& camera, const camera::Viewport& viewport,
                                     const camera::Pixel& pixel, bool onRay) const override;
    camera::Vec3 gravityPoint(const camera::Camera& camera, const camera::Viewport& viewport) const override;

private:
    const pick::RayScene* rays() const;

    const render::RhiView* m_view;
    const viewer::OcctViewWidget* m_hiddenView;
    mutable std::vector<std::shared_ptr<const snapshot::Body>> m_bodies;
    mutable std::unique_ptr<pick::RayScene> m_rays;
};

class NewViewNavigation : public QObject {
    Q_OBJECT

public:
    NewViewNavigation(render::RhiView& view, const viewer::OcctViewWidget& hiddenView, CameraCommands& commands);

    void setNavigation(const camera::Navigation& navigation);

signals:
    void menuRequested(QPoint globalPosition);

private:
    void press(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
    void release(QPointF position, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
    void move(QPointF position, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
    void wheel(QPointF position, double notches);
    std::optional<camera::Pixel> pixelAt(QPointF position) const;
    void flush();

    render::RhiView* m_view;
    CameraCommands* m_commands;
    camera::Navigator m_navigator;
    SnapshotNavigationScene m_scene;
};

} // namespace kalve137::app
