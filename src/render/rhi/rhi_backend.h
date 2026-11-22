// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/draw_list.h"

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
    struct Gpu {
        std::uint64_t meshRevision = 0;
        std::uint64_t paletteRevision = 0;
        Owned<QRhiBuffer> vertices;
        Owned<QRhiBuffer> indices;
        Owned<QRhiBuffer> segments;
        Palette faces;
        Palette edges;
        std::uint32_t indexCount = 0;
        std::uint32_t segmentCount = 0;
        bool used = false;
    };
    struct Call {
        const Gpu* gpu = nullptr;
        bool cull = false;
    };

    bool createShared();
    Owned<QRhiShaderResourceBindings> bindingsFor(QRhiTexture* palette) const;
    Owned<QRhiGraphicsPipeline> facePipeline(QRhiRenderPassDescriptor* pass, int samples, bool cull, bool biased) const;
    Owned<QRhiGraphicsPipeline> edgePipeline(QRhiRenderPassDescriptor* pass, int samples) const;
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
    std::unordered_map<std::uint64_t, Gpu> m_chunks;
    std::vector<Call> m_calls;
    std::vector<const Gpu*> m_edgeCalls;
    bool m_biased = false;
    bool m_cornersUploaded = false;
    std::uint64_t m_meshUploads = 0;
    std::uint64_t m_paletteUploads = 0;
};

} // namespace kalve137::render
