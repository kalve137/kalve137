// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/navigator.h"

#include <QObject>
#include <QPoint>
#include <QPointF>

#include <optional>

namespace kalve137::render {
class RhiView;
}

namespace kalve137::app {

class CameraCommands;

class SnapshotNavigationScene : public camera::NavigationScene {
public:
    explicit SnapshotNavigationScene(render::RhiView& view);

    std::optional<camera::Vec3> pick(const camera::Camera& camera, const camera::Viewport& viewport,
                                     const camera::Pixel& pixel, bool onRay) const override;
    camera::Vec3 gravityPoint(const camera::Camera& camera, const camera::Viewport& viewport) const override;

private:
    render::RhiView* m_view;
};

class NewViewNavigation : public QObject {
    Q_OBJECT

public:
    NewViewNavigation(render::RhiView& view, CameraCommands& commands);

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
