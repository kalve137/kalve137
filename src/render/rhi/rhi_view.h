// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "render/graphics_api.h"
#include "render/scene_cache.h"
#include "render/style.h"
#include "snapshot/body.h"

#include <QPointF>
#include <QRhiWidget>
#include <QString>

#include <memory>
#include <optional>

class QLabel;
class QRhiCommandBuffer;
class QRhiRenderTarget;

namespace kalve137::render {

class RhiBackend;

bool canStart(GraphicsApi api);
QLabel* makeFailureNotice(GraphicsApi api, QWidget* parent);

class RhiView : public QRhiWidget {
    Q_OBJECT

public:
    explicit RhiView(GraphicsApi api, QWidget* parent = nullptr);
    ~RhiView() override;
    RhiView(const RhiView&) = delete;
    RhiView& operator=(const RhiView&) = delete;
    RhiView(RhiView&&) = delete;
    RhiView& operator=(RhiView&&) = delete;

    void setViewportStyle(const ViewportStyle& style);
    const ViewportStyle& viewportStyle() const { return m_style; }
    void setScene(snapshot::Scene scene);
    const snapshot::Scene& scene() const { return m_scene; }
    void setCamera(const camera::Camera& camera);
    const camera::Camera& camera() const { return m_camera; }
    camera::Viewport viewport() const;

    bool isReady() const { return m_ready; }
    bool hasFailed() const { return m_failed; }
    QString backendName() const { return m_backend; }
    QString deviceName() const { return m_device; }
    std::optional<double> benchFrame();

signals:
    void ready();
    void resized();
    void mousePressed(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons,
                      Qt::KeyboardModifiers modifiers);
    void mouseReleased(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons,
                       Qt::KeyboardModifiers modifiers);
    void mouseMoved(QPointF position, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
    void wheelTurned(QPointF position, double notches);

protected:
    void initialize(QRhiCommandBuffer* buffer) override;
    void render(QRhiCommandBuffer* buffer) override;
    void releaseResources() override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void record(QRhiCommandBuffer* buffer, QRhiRenderTarget* target);
    QRhiTexture* resolvedTexture() const;
    void fail();
    void placeMessage();

    GraphicsApi m_api;
    ViewportStyle m_style;
    snapshot::Scene m_scene;
    camera::Camera m_camera;
    SceneCache m_cache;
    std::unique_ptr<RhiBackend> m_bodies;
    QString m_backend;
    QString m_device;
    QLabel* m_message = nullptr;
    bool m_ready = false;
    bool m_failed = false;
};

} // namespace kalve137::render
