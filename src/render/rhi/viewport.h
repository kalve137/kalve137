// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "render/annotations.h"
#include "render/chunk_set.h"
#include "render/datums.h"
#include "render/graphics_api.h"
#include "render/grid_overlay.h"
#include "render/handles.h"
#include "render/pick_id.h"
#include "render/sketches.h"
#include "render/style.h"
#include "snapshot/body.h"
#include "snapshot/sketch.h"

#include <QPointF>
#include <QRect>
#include <QRhiWidget>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class QLabel;
class QRhiCommandBuffer;
class QRhiRenderTarget;

namespace kalve137::render {

class RhiBackend;

inline constexpr std::uint64_t kPickChunkIds = std::uint64_t{1} << 48;

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
    std::optional<DatumKey> datum;
    std::optional<SketchKey> sketch;
};

StartProblem startProblem(const DeviceProbe& probe);
StartProblem checkStart(GraphicsApi api);
bool canStart(GraphicsApi api);
QLabel* makeFailureNotice(GraphicsApi api, StartProblem problem, QWidget* parent);

class Viewport : public QRhiWidget {
    Q_OBJECT

public:
    explicit Viewport(GraphicsApi api, QWidget* parent = nullptr);
    ~Viewport() override;
    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;
    Viewport(Viewport&&) = delete;
    Viewport& operator=(Viewport&&) = delete;

    void setViewportStyle(const ViewportStyle& style);
    const ViewportStyle& viewportStyle() const { return m_style; }
    void setGrid(const GridSettings& settings);
    const GridOverlay& grid() const { return m_grid; }
    void setDatums(Datums datums);
    const Datums& datums() const { return m_datums; }
    void setDatumMarks(DatumMarks marks);
    const DatumMarks& datumMarks() const { return m_datumMarks; }
    DatumDraw datumDraw() const;
    void setMarkers(std::vector<MarkerItem> markers);
    const std::vector<MarkerItem>& markers() const { return m_markers; }
    MarkerDraw markerDraw() const;
    std::optional<camera::Box> fitBounds(double emptyHalfSize) const;
    void setScene(snapshot::Scene scene);
    const snapshot::Scene& scene() const { return m_scene; }
    void setPickScene(std::optional<snapshot::Scene> scene);
    const snapshot::Scene& pickScene() const { return m_pickScene ? *m_pickScene : m_scene; }
    const std::optional<camera::Box>& sceneBounds() const { return m_bounds; }
    void setSketches(std::vector<snapshot::Sketch> sketches);
    const std::vector<snapshot::Sketch>& sketches() const { return m_sketches; }
    void setSketchLook(SketchLook look);
    const SketchLook& sketchLook() const { return m_sketchLook; }
    void setSketchMarks(SketchMarks marks);
    const SketchGeometry& sketchGeometry() const { return m_sketchGeometry; }
    SketchDraw sketchDraw() const;
    void setActiveSketch(ActiveSketch sketch);
    const ActiveSketch& activeSketch() const { return m_activeSketch; }
    void setSketchPreview(SketchPreview preview);
    const SketchPreview& sketchPreview() const { return m_sketchPreview; }
    ActiveSketchDraw activeSketchDraw() const;
    void setAnnotations(Annotations annotations);
    const Annotations& annotations() const { return m_annotations; }
    AnnotationDraw annotationDraw() const;
    void setHandles(std::vector<HandleItem> handles);
    const std::vector<HandleItem>& handles() const { return m_handles; }
    HandleDraw handleDraw() const;
    void setTextFamily(const QString& family);
    void setHighlight(Highlight highlight);
    void setHover(std::set<PickedItem> hovered, std::set<PickedItem> hoveredOutline);
    void setCamera(const camera::Camera& camera);
    const camera::Camera& camera() const { return m_camera; }
    void setLeftButtonCaptured(bool captured) { m_leftCaptured = captured; }
    bool isLeftButtonCaptured() const { return m_leftCaptured; }
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
    std::optional<camera::Vec3> pointOf(int x, int y, float depth, DepthReach reach) const;
    std::optional<camera::Vec3> surfaceAt(int x, int y);
    std::optional<std::vector<PickedItem>> itemsIn(const QRect& deviceRect, bool whole, const PickKinds& kinds,
                                                   const IdStyle& style = {});
    std::uint64_t idPasses() const { return m_idPasses; }

signals:
    void ready();
    void resized();
    void sceneChanged();
    void cameraChanged();
    void mouseLeft();
    void mousePressed(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons,
                      Qt::KeyboardModifiers modifiers);
    void mouseReleased(QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons,
                       Qt::KeyboardModifiers modifiers);
    void mouseMoved(QPointF position, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
    void wheelTurned(QPointF position, double notches);
    void capturedPress(QPointF position, Qt::KeyboardModifiers modifiers);
    void capturedRelease(QPointF position);
    void capturedDoubleClick(QPointF position);

protected:
    void initialize(QRhiCommandBuffer* buffer) override;
    void render(QRhiCommandBuffer* buffer) override;
    void releaseResources() override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct IdTargets;

    void updateChunks();
    const ChunkSet& pickChunks() const { return m_pickScene ? m_pickChunks : m_chunks; }
    void refreshDatums();
    void refreshMarkers();
    void refreshPreview();
    std::optional<camera::Box> activeBounds() const;
    void refreshSketches();
    void refreshAnnotations();
    void refreshHandles();
    void refreshActiveSketch();
    void record(QRhiCommandBuffer* buffer, QRhiRenderTarget* target);
    bool ensureIdTargets(QSize size);
    QRhiTexture* resolvedTexture() const;
    void fail();
    void placeMessage();

    GraphicsApi m_api;
    ViewportStyle m_style;
    snapshot::Scene m_scene;
    std::optional<camera::Box> m_bounds;
    std::vector<snapshot::Sketch> m_sketches;
    std::optional<camera::Box> m_sketchBounds;
    SketchLook m_sketchLook;
    SketchMarks m_sketchMarks;
    SketchGeometry m_sketchGeometry;
    std::uint64_t m_sketchRevision = 0;
    ActiveSketch m_activeSketch;
    std::optional<camera::Box> m_activeBounds;
    ActiveGeometry m_activeGeometry;
    std::uint64_t m_activeRevision = 0;
    SketchPreview m_sketchPreview;
    std::optional<camera::Box> m_previewBounds;
    std::vector<DatumLine> m_previewLines;
    std::uint64_t m_previewRevision = 0;
    camera::Camera m_camera;
    GridOverlay m_grid;
    Datums m_datums;
    DatumMarks m_datumMarks;
    DatumGeometry m_datumGeometry;
    std::uint64_t m_datumRevision = 0;
    std::vector<MarkerItem> m_markers;
    std::vector<Marker> m_markerGeometry;
    std::uint64_t m_markerRevision = 0;
    Annotations m_annotations;
    PlacedAnnotations m_placedAnnotations;
    std::uint64_t m_annotationRevision = 0;
    std::vector<HandleItem> m_handles;
    HandleGeometry m_handleGeometry;
    std::uint64_t m_handleRevision = 0;
    QString m_textFamily = QStringLiteral("Roboto");
    ChunkSet m_chunks;
    std::optional<snapshot::Scene> m_pickScene;
    ChunkSet m_pickChunks{ChunkSet::kDefaultVertexBudget, kPickChunkIds};
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
    bool m_leftCaptured = false;
};

} // namespace kalve137::render
