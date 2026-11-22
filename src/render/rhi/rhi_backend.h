// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/draw_list.h"
#include "render/pick_id.h"

#include <QSize>

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
    enum class Input { Lines, Positions, None };
    enum class Depth { Off, LessOrEqual, Greater };
    enum class Blend { Opaque, Mask, Constant };
    struct PipelineSpec {
        const char* vertex = nullptr;
        const char* fragment = nullptr;
        Input input = Input::Lines;
        Depth depth = Depth::Off;
        int biasUnits = 0;
        float biasSlope = 0.0F;
        Blend blend = Blend::Opaque;
        bool scissor = false;
    };

    bool createShared();
    Owned<QRhiShaderResourceBindings> bindingsFor(QRhiTexture* palette) const;
    Owned<QRhiGraphicsPipeline> facePipeline(QRhiRenderPassDescriptor* pass, int samples, bool cull, bool biased) const;
    Owned<QRhiGraphicsPipeline> edgePipeline(QRhiRenderPassDescriptor* pass, int samples) const;
    Owned<QRhiGraphicsPipeline> idPipeline(QRhiRenderPassDescriptor* pass, PickKind kind, bool cull) const;
    Owned<QRhiShaderResourceBindings> idBindingsFor(QRhiTexture* palette, QRhiBuffer* pick) const;
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
    bool upload(const void* data, std::size_t bytes, std::size_t count, Geometry& geometry,
                QRhiResourceUpdateBatch* updates);
    void drawLines(QRhiCommandBuffer* buffer, QRhiGraphicsPipeline* pipeline, const Geometry& lines,
                   const QRhiViewport& viewport) const;
    void drawTriangles(QRhiCommandBuffer* buffer, QRhiGraphicsPipeline* pipeline, const Geometry& triangles,
                       const QRhiViewport& viewport, float weight) const;

    QRhi* m_rhi;
    Owned<QRhiBuffer> m_uniforms;
    Owned<QRhiBuffer> m_corners;
    Owned<QRhiSampler> m_sampler;
    Owned<QRhiTexture> m_layoutTexture;
    Owned<QRhiShaderResourceBindings> m_layout;
    std::array<Owned<QRhiGraphicsPipeline>, 4> m_faces;
    Owned<QRhiGraphicsPipeline> m_edges;
    Owned<QRhiGraphicsPipeline> m_halo;
    Owned<QRhiGraphicsPipeline> m_maskFill;
    Owned<QRhiGraphicsPipeline> m_goldFill;
    std::array<Owned<QRhiGraphicsPipeline>, 2> m_hiddenFaces;
    Owned<QRhiGraphicsPipeline> m_hiddenEdges;
    HighlightGpu m_highlight;
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
    std::unordered_map<std::uint64_t, Gpu> m_chunks;
    std::vector<Call> m_calls;
    std::vector<const Gpu*> m_edgeCalls;
    std::vector<IdCall> m_idCalls;
    std::vector<std::uint64_t> m_idChunks;
    bool m_biased = false;
    bool m_cornersUploaded = false;
    bool m_toldPickLimit = false;
    std::uint64_t m_meshUploads = 0;
    std::uint64_t m_paletteUploads = 0;
};

} // namespace kalve137::render
