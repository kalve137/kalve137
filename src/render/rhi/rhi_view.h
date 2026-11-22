// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "render/chunk_set.h"
#include "render/graphics_api.h"
#include "render/grid_overlay.h"
#include "render/pick_id.h"
#include "render/style.h"
#include "snapshot/body.h"

#include <QPointF>
#include <QRect>
#include <QRhiWidget>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>

class QLabel;
class QRhiCommandBuffer;
class QRhiRenderTarget;

namespace kalve137::render {

class RhiBackend;

struct DeviceProbe {
    bool created = false;
    bool texelFetch = false;
    bool openGl = false;
    bool openGlEs = false;
    int major = 0;
    int minor = 0;
};

enum class StartProblem { None, NoDevice, TooOld };

struct IdPick {
    PickedItem item;
    IdHit hit;
};

StartProblem startProblem(const DeviceProbe& probe);
StartProblem checkStart(GraphicsApi api);
bool canStart(GraphicsApi api);
QLabel* makeFailureNotice(GraphicsApi api, StartProblem problem, QWidget* parent);

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
    void setGrid(const GridSettings& settings);
    const GridOverlay& grid() const { return m_grid; }
    void setScene(snapshot::Scene scene);
    const snapshot::Scene& scene() const { return m_scene; }
    const std::optional<camera::Box>& sceneBounds() const { return m_bounds; }
    void setHighlight(Highlight highlight);
    void setHover(std::set<PickedItem> hovered, std::set<PickedItem> hoveredOutline);
    void setCamera(const camera::Camera& camera);
    const camera::Camera& camera() const { return m_camera; }
    camera::Viewport viewport() const;
    Frame frame() const;

    bool isReady() const { return m_ready; }
    bool hasFailed() const { return m_failed; }
    QString backendName() const { return m_backend; }
    QString deviceName() const { return m_device; }
    const ChunkSet& chunks() const { return m_chunks; }
    const RhiBackend* backend() const { return m_bodies.get(); }
    std::optional<double> benchFrame();
    double lastUpdateMs() const { return m_updateMs; }
    std::optional<IdImage> readIds(const QRect& deviceRect, const IdStyle& style = {});
    std::optional<IdPick> pickAt(QPointF position, const PickKinds& kinds, const IdStyle& style = {});
    camera::Vec3 pointOf(int x, int y, float depth) const;
    std::optional<camera::Vec3> surfaceAt(int x, int y);
    std::optional<std::vector<PickedItem>> itemsIn(const QRect& deviceRect, bool whole, const PickKinds& kinds,
                                                   const IdStyle& style = {});
    std::uint64_t idPasses() const { return m_idPasses; }

signals:
    void ready();
    void resized();
    void sceneChanged();
    void mouseLeft();
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
    void leaveEvent(QEvent* event) override;

private:
    struct IdTargets;

    void updateChunks();
    void record(QRhiCommandBuffer* buffer, QRhiRenderTarget* target);
    bool ensureIdTargets(QSize size);
    QRhiTexture* resolvedTexture() const;
    void fail();
    void placeMessage();

    GraphicsApi m_api;
    ViewportStyle m_style;
    snapshot::Scene m_scene;
    std::optional<camera::Box> m_bounds;
    camera::Camera m_camera;
    GridOverlay m_grid;
    ChunkSet m_chunks;
    std::unique_ptr<RhiBackend> m_bodies;
    std::unique_ptr<IdTargets> m_ids;
    IdStyle m_idStyle;
    bool m_idsFresh = false;
    bool m_idsFailed = false;
    bool m_idsWarm = false;
    std::uint64_t m_idPasses = 0;
    QString m_backend;
    QString m_device;
    QLabel* m_message = nullptr;
    double m_updateMs = 0.0;
    bool m_ready = false;
    bool m_failed = false;
};

} // namespace kalve137::render
