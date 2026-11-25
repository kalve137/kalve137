// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/draw_list.h"
#include "render/pick_id.h"

#include <QSize>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiResource;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;
class QRhiViewport;

namespace kalve137::render {

class GlyphAtlas;

class RhiBackend {
public:
    explicit RhiBackend(QRhi* rhi);
    ~RhiBackend();
    RhiBackend(const RhiBackend&) = delete;
    RhiBackend& operator=(const RhiBackend&) = delete;
    RhiBackend(RhiBackend&&) = delete;
    RhiBackend& operator=(RhiBackend&&) = delete;

    bool setTarget(QRhiRenderPassDescriptor* pass, int samples);
    bool setPostTarget(QRhiTexture* resolved);
    bool prepare(const DrawList& list, QRhiResourceUpdateBatch* updates);
    void draw(QRhiCommandBuffer* buffer, QSize size) const;
    bool needsPost() const { return m_postNeeded; }
    void drawPost(QRhiCommandBuffer* buffer) const;
    bool setIdTarget(QRhiRenderPassDescriptor* pass);
    bool prepareIds(const DrawList& list, const IdStyle& style, QRhiResourceUpdateBatch* updates);
    void drawIds(QRhiCommandBuffer* buffer, QSize size) const;
    const std::vector<std::uint64_t>& idChunks() const { return m_idChunks; }
    std::size_t chunkCount() const { return m_chunks.size(); }
    std::uint64_t meshUploads() const { return m_meshUploads; }
    std::uint64_t paletteUploads() const { return m_paletteUploads; }
    void setTextFamily(const QString& family);
    std::uint32_t annotationQuads() const { return m_annotations.quads.count; }

private:
    struct Release {
        void operator()(QRhiResource* resource) const;
    };
    template <typename T>
    using Owned = std::unique_ptr<T, Release>;

    struct Palette {
        Owned<QRhiTexture> texture;
        Owned<QRhiShaderResourceBindings> bindings;
    };
    struct IdDraw {
        Owned<QRhiBuffer> pick;
        Owned<QRhiShaderResourceBindings> bindings;
        const QRhiTexture* palette = nullptr;
    };
    struct Gpu {
        std::uint64_t meshRevision = 0;
        std::uint64_t paletteRevision = 0;
        Owned<QRhiBuffer> vertices;
        Owned<QRhiBuffer> indices;
        Owned<QRhiBuffer> segments;
        Owned<QRhiBuffer> points;
        Palette faces;
        Palette edges;
        Palette pointPalette;
        std::array<IdDraw, 3> ids;
        std::uint32_t indexCount = 0;
        std::uint32_t segmentCount = 0;
        std::uint32_t pointCount = 0;
        bool used = false;
    };
    struct Call {
        const Gpu* gpu = nullptr;
        bool cull = false;
    };
    struct TransparentCall {
        const Gpu* gpu = nullptr;
        BodyRange range;
        float opacity = 1.0F;
        bool cull = false;
    };
    struct IdCall {
        const Gpu* gpu = nullptr;
        PickKind kind = PickKind::None;
        bool cull = false;
    };
    struct Geometry {
        Owned<QRhiBuffer> buffer;
        std::uint32_t count = 0;
    };
    struct HighlightGpu {
        std::uint64_t revision = 0;
        Geometry lines;
        Geometry selectedEdges;
        Geometry selectedFaces;
        Geometry hoveredFaces;
    };
    struct GridGpu {
        std::uint64_t revision = 0;
        Geometry wash;
        Geometry lines;
        Owned<QRhiShaderResourceBindings> washBindings;
        Owned<QRhiShaderResourceBindings> lineBindings;
    };
    struct DatumGpu {
        std::uint64_t revision = 0;
        Geometry fills;
        Geometry lines;
        Geometry originFills;
        Geometry originLines;
        Geometry idFills;
        Geometry idOutlines;
        Geometry idLines;
        Geometry idOriginFills;
        Geometry idOriginOutlines;
        Geometry idOriginLines;
        Owned<QRhiShaderResourceBindings> fillBindings;
        Owned<QRhiShaderResourceBindings> lineBindings;
        Owned<QRhiShaderResourceBindings> originFillBindings;
        Owned<QRhiShaderResourceBindings> originLineBindings;
        Owned<QRhiShaderResourceBindings> resetBindings;
        Owned<QRhiBuffer> pick;
        Owned<QRhiBuffer> originPick;
        Owned<QRhiShaderResourceBindings> idFillBindings;
        Owned<QRhiShaderResourceBindings> idLineBindings;
        Owned<QRhiShaderResourceBindings> originIdFillBindings;
        Owned<QRhiShaderResourceBindings> originIdLineBindings;
    };
    struct MarkerGpu {
        std::uint64_t revision = 0;
        Geometry depth;
        Geometry top;
        Geometry ids;
        Geometry sketchIds;
        Owned<QRhiShaderResourceBindings> depthBindings;
        Owned<QRhiShaderResourceBindings> topBindings;
        Owned<QRhiBuffer> pick;
        Owned<QRhiShaderResourceBindings> idBindings;
    };
    struct SketchGpu {
        std::uint64_t revision = 0;
        Geometry curves;
        Geometry profiles;
        Geometry idCurves;
        Geometry idProfiles;
        Owned<QRhiShaderResourceBindings> curveBindings;
        Owned<QRhiShaderResourceBindings> profileBindings;
        Owned<QRhiBuffer> pick;
        Owned<QRhiShaderResourceBindings> idCurveBindings;
        Owned<QRhiShaderResourceBindings> idProfileBindings;
    };
    struct ActiveGpu {
        std::uint64_t revision = 0;
        Geometry profiles;
        Geometry lines;
        Geometry arrows;
        std::uint64_t previewRevision = 0;
        Geometry previewLines;
        Owned<QRhiShaderResourceBindings> fillBindings;
        Owned<QRhiShaderResourceBindings> lineBindings;
    };
    struct AnnotationGpu {
        std::uint64_t revision = 0;
        std::uint64_t atlasRevision = 0;
        Geometry quads;
        Owned<QRhiTexture> atlas;
        Owned<QRhiShaderResourceBindings> bindings;
    };
    struct HandleGpu {
        std::uint64_t revision = 0;
        Geometry arrows;
        Geometry guides;
        Owned<QRhiShaderResourceBindings> arrowBindings;
        Owned<QRhiShaderResourceBindings> guideBindings;
    };
    enum class Input { Lines, Positions, OverlayLines, OverlayFill, DatumLines, Markers, Annotations, Handles, None };
    enum class Depth { Off, LessOrEqual, Greater, Write, Reset };
    enum class Blend { Opaque, Mask, Constant, Alpha, Hidden };
    struct PipelineSpec {
        const char* vertex = nullptr;
        const char* fragment = nullptr;
        Input input = Input::Lines;
        Depth depth = Depth::Off;
        int biasUnits = 0;
        float biasSlope = 0.0F;
        Blend blend = Blend::Opaque;
        bool scissor = false;
        int targets = 1;
        bool clamp = false;
    };

    bool createShared();
    Owned<QRhiShaderResourceBindings> bindingsFor(QRhiTexture* palette) const;
    Owned<QRhiShaderResourceBindings> bindingsWith(QRhiTexture* palette, QRhiBuffer* uniforms) const;
    Owned<QRhiGraphicsPipeline> facePipeline(QRhiRenderPassDescriptor* pass, int samples, bool cull, bool biased,
                                             bool transparent) const;
    bool createTransparentPipelines(QRhiRenderPassDescriptor* pass, int samples);
    void prepareTransparent(const DrawList& list, const std::vector<const Gpu*>& placed);
    void drawTransparent(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const;
    Owned<QRhiGraphicsPipeline> edgePipeline(QRhiRenderPassDescriptor* pass, int samples, bool transparent) const;
    Owned<QRhiGraphicsPipeline> idPipeline(QRhiRenderPassDescriptor* pass, PickKind kind, bool cull) const;
    Owned<QRhiShaderResourceBindings> idBindingsFor(QRhiTexture* palette, QRhiBuffer* pick) const;
    Owned<QRhiShaderResourceBindings> idBindingsWith(QRhiTexture* palette, QRhiBuffer* uniforms,
                                                     QRhiBuffer* pick) const;
    bool refreshIds(Gpu& gpu, std::uint32_t place, double edgeWidth, double pointWidth,
                    QRhiResourceUpdateBatch* updates);
    void writeUniforms(const DrawList& list, QRhiResourceUpdateBatch* updates);
    bool refreshChunks(const DrawList& list, QRhiResourceUpdateBatch* updates);
    bool refresh(const Chunk& chunk, Gpu& gpu, QRhiResourceUpdateBatch* updates);
    bool refreshMesh(const Chunk& chunk, Gpu& gpu, QRhiResourceUpdateBatch* updates);
    bool refreshPalette(std::span<const Entry> entries, Palette& palette, QRhiResourceUpdateBatch* updates);
    Owned<QRhiGraphicsPipeline> pipelineFor(const PipelineSpec& spec, QRhiRenderPassDescriptor* pass, int samples,
                                            QRhiShaderResourceBindings* bindings) const;
    bool refreshHighlight(const HighlightDraw& highlight, QRhiResourceUpdateBatch* updates);
    bool refreshGrid(const GridDraw& grid, QRhiResourceUpdateBatch* updates);
    void drawGrid(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const;
    bool refreshDatums(const DatumDraw& datums, QRhiResourceUpdateBatch* updates);
    void drawDatums(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const;
    void drawOrigin(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const;
    void drawDatumIds(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const;
    bool refreshMarkers(const MarkerDraw& markers, QRhiResourceUpdateBatch* updates);
    bool refreshSketches(const SketchDraw& sketches, QRhiResourceUpdateBatch* updates);
    bool createSketchShared();
    bool refreshActiveSketch(const ActiveSketchDraw& sketch, QRhiResourceUpdateBatch* updates);
    void drawActiveSketch(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const;
    bool refreshAnnotations(const AnnotationDraw& annotations, double ratio, QRhiResourceUpdateBatch* updates);
    bool uploadAtlas(QRhiResourceUpdateBatch* updates);
    bool createHandleShared();
    bool createHandlePipelines(QRhiRenderPassDescriptor* pass, int samples);
    bool refreshHandles(const HandleDraw& handles, QRhiResourceUpdateBatch* updates);
    void drawHandles(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const;
    void drawFill(QRhiCommandBuffer* buffer, QRhiGraphicsPipeline* pipeline, QRhiShaderResourceBindings* bindings,
                  const Geometry& fill, const QRhiViewport& viewport) const;
    bool upload(const void* data, std::size_t bytes, std::size_t count, Geometry& geometry,
                QRhiResourceUpdateBatch* updates);
    bool uploadInto(const void* data, std::size_t bytes, std::size_t count, Geometry& geometry,
                    QRhiResourceUpdateBatch* updates);
    void drawLines(QRhiCommandBuffer* buffer, QRhiGraphicsPipeline* pipeline, QRhiShaderResourceBindings* bindings,
                   const Geometry& lines, const QRhiViewport& viewport) const;
    void drawTriangles(QRhiCommandBuffer* buffer, QRhiGraphicsPipeline* pipeline, QRhiShaderResourceBindings* bindings,
                       const Geometry& triangles, const QRhiViewport& viewport, float weight) const;

    QRhi* m_rhi;
    Owned<QRhiBuffer> m_uniforms;
    Owned<QRhiBuffer> m_originUniforms;
    Owned<QRhiBuffer> m_sketchUniforms;
    Owned<QRhiBuffer> m_corners;
    Owned<QRhiSampler> m_sampler;
    Owned<QRhiTexture> m_layoutTexture;
    Owned<QRhiShaderResourceBindings> m_layout;
    Owned<QRhiShaderResourceBindings> m_transparentLayout;
    Owned<QRhiShaderResourceBindings> m_transparentEdgeLayout;
    std::array<Owned<QRhiGraphicsPipeline>, 4> m_faces;
    std::array<Owned<QRhiGraphicsPipeline>, 4> m_transparentFaces;
    Owned<QRhiGraphicsPipeline> m_edges;
    Owned<QRhiGraphicsPipeline> m_transparentEdges;
    Owned<QRhiGraphicsPipeline> m_halo;
    Owned<QRhiGraphicsPipeline> m_maskFill;
    Owned<QRhiGraphicsPipeline> m_goldFill;
    std::array<Owned<QRhiGraphicsPipeline>, 2> m_hiddenFaces;
    Owned<QRhiGraphicsPipeline> m_hiddenEdges;
    Owned<QRhiShaderResourceBindings> m_haloBindings;
    Owned<QRhiShaderResourceBindings> m_maskBindings;
    Owned<QRhiShaderResourceBindings> m_goldBindings;
    std::array<Owned<QRhiShaderResourceBindings>, 2> m_hiddenFaceBindings;
    Owned<QRhiShaderResourceBindings> m_hiddenEdgeBindings;
    Owned<QRhiGraphicsPipeline> m_gridWash;
    Owned<QRhiGraphicsPipeline> m_gridLines;
    Owned<QRhiGraphicsPipeline> m_datumFill;
    Owned<QRhiGraphicsPipeline> m_datumLines;
    Owned<QRhiGraphicsPipeline> m_originFill;
    Owned<QRhiGraphicsPipeline> m_originLines;
    Owned<QRhiGraphicsPipeline> m_depthReset;
    Owned<QRhiGraphicsPipeline> m_markerDepth;
    Owned<QRhiGraphicsPipeline> m_markerTop;
    Owned<QRhiGraphicsPipeline> m_sketchCurves;
    Owned<QRhiGraphicsPipeline> m_sketchProfiles;
    Owned<QRhiGraphicsPipeline> m_activeFills;
    Owned<QRhiGraphicsPipeline> m_activeLines;
    Owned<QRhiGraphicsPipeline> m_annotationQuads;
    Owned<QRhiGraphicsPipeline> m_handleArrows;
    Owned<QRhiGraphicsPipeline> m_handleGuides;
    HandleGpu m_handles;
    QString m_textFamily = QStringLiteral("Roboto");
    std::unique_ptr<GlyphAtlas> m_atlas;
    AnnotationGpu m_annotations;
    HighlightGpu m_highlight;
    GridGpu m_grid;
    DatumGpu m_datums;
    MarkerGpu m_markers;
    SketchGpu m_sketches;
    ActiveGpu m_active;
    bool m_hiddenPass = false;
    bool m_wireFill = false;
    bool m_postNeeded = false;
    PixelRect m_postRect;
    QRhiTexture* m_resolved = nullptr;
    Owned<QRhiTexture> m_postSource;
    Owned<QRhiTextureRenderTarget> m_postTarget;
    Owned<QRhiRenderPassDescriptor> m_postPass;
    Owned<QRhiShaderResourceBindings> m_postBindings;
    Owned<QRhiGraphicsPipeline> m_post;
    Owned<QRhiShaderResourceBindings> m_idLayout;
    std::array<Owned<QRhiGraphicsPipeline>, 3> m_idPipelines;
    Owned<QRhiGraphicsPipeline> m_idDatumFill;
    Owned<QRhiGraphicsPipeline> m_idDatumLines;
    Owned<QRhiGraphicsPipeline> m_idReset;
    Owned<QRhiGraphicsPipeline> m_idMarkers;
    Owned<QRhiGraphicsPipeline> m_idSketchProfiles;
    bool m_idPlanes = false;
    bool m_idAxes = false;
    bool m_idDatumsThroughBodies = false;
    bool m_idMarkersShown = false;
    bool m_idSketchLines = false;
    bool m_idSketchPoints = false;
    bool m_idProfiles = false;
    std::unordered_map<std::uint64_t, Gpu> m_chunks;
    std::vector<Call> m_calls;
    std::vector<const Gpu*> m_edgeCalls;
    std::vector<TransparentCall> m_transparentCalls;
    bool m_transparentFacesShown = false;
    bool m_transparentEdgesShown = false;
    std::vector<IdCall> m_idCalls;
    std::vector<std::uint64_t> m_idChunks;
    bool m_biased = false;
    bool m_cornersUploaded = false;
    bool m_toldPickLimit = false;
    bool m_annotationsFailed = false;
    std::uint64_t m_meshUploads = 0;
    std::uint64_t m_paletteUploads = 0;
};

} // namespace kalve137::render
