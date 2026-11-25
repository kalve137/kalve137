// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"

#include <gp_Pnt.hxx>

#include <QObject>
#include <QPointF>

#include <optional>

class QWidget;

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::render {
class Viewport;
}

namespace kalve137::app {

class ViewProjection : public QObject {
    Q_OBJECT

public:
    struct Projection {
        camera::Camera camera;
        camera::Viewport viewport;
    };

    ViewProjection(viewer::OcctViewWidget& view, render::Viewport* viewport, QObject* parent = nullptr);

    viewer::OcctViewWidget& view() const { return m_view; }
    render::Viewport* viewport() const { return m_viewport; }
    QWidget* canvas() const;
    void setCameraInput(bool camera) { m_cameraInput = camera; }
    bool hasCameraInput() const { return m_cameraInput; }

    std::optional<Projection> projection() const;
    double worldPerPixel() const;
    QPointF toScreen(const gp_Pnt& point) const;

signals:
    void changed();

private:
    viewer::OcctViewWidget& m_view;
    render::Viewport* m_viewport;
    bool m_cameraInput;
};

} // namespace kalve137::app
