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
    bool prepare(const DrawList& list, QRhiResourceUpdateBatch* updates);
    void draw(QRhiCommandBuffer* buffer, QSize size) const;
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

    QRhi* m_rhi;
    Owned<QRhiBuffer> m_uniforms;
    Owned<QRhiBuffer> m_corners;
    Owned<QRhiSampler> m_sampler;
    Owned<QRhiTexture> m_layoutTexture;
    Owned<QRhiShaderResourceBindings> m_layout;
    std::array<Owned<QRhiGraphicsPipeline>, 4> m_faces;
    Owned<QRhiGraphicsPipeline> m_edges;
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
