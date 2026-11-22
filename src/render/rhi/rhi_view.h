// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/graphics_api.h"
#include "render/style.h"
#include "snapshot/body.h"

#include <QRhiWidget>
#include <QString>

#include <optional>

class QLabel;
class QRhiCommandBuffer;
class QRhiRenderTarget;

namespace kalve137::render {

bool canStart(GraphicsApi api);
QLabel* makeFailureNotice(GraphicsApi api, QWidget* parent);

class RhiView : public QRhiWidget {
    Q_OBJECT

public:
    explicit RhiView(GraphicsApi api, QWidget* parent = nullptr);

    void setViewportStyle(const ViewportStyle& style);
    const ViewportStyle& viewportStyle() const { return m_style; }
    void setScene(snapshot::Scene scene);
    const snapshot::Scene& scene() const { return m_scene; }

    bool isReady() const { return m_ready; }
    bool hasFailed() const { return m_failed; }
    QString backendName() const { return m_backend; }
    QString deviceName() const { return m_device; }
    std::optional<double> benchFrame();

signals:
    void ready();

protected:
    void initialize(QRhiCommandBuffer* buffer) override;
    void render(QRhiCommandBuffer* buffer) override;
    void releaseResources() override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void record(QRhiCommandBuffer* buffer, QRhiRenderTarget* target);
    void fail();
    void placeMessage();

    GraphicsApi m_api;
    ViewportStyle m_style;
    snapshot::Scene m_scene;
    QString m_backend;
    QString m_device;
    QLabel* m_message = nullptr;
    bool m_ready = false;
    bool m_failed = false;
};

} // namespace kalve137::render
