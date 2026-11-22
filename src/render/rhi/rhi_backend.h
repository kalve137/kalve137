// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/draw_list.h"

#include <QSize>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiResource;
class QRhiResourceUpdateBatch;
class QRhiShaderResourceBindings;

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
    std::size_t bufferCount() const { return m_buffers.size(); }

private:
    struct Release {
        void operator()(QRhiResource* resource) const;
    };
    template <typename T>
    using Owned = std::unique_ptr<T, Release>;

    struct Gpu {
        Owned<QRhiBuffer> buffer;
        bool used = false;
    };
    struct Call {
        QRhiBuffer* positions = nullptr;
        QRhiBuffer* normals = nullptr;
        QRhiBuffer* colors = nullptr;
        QRhiBuffer* indices = nullptr;
        std::uint32_t indexCount = 0;
        bool lit = true;
    };

    enum class Slot : std::uint8_t { Positions, Normals, Colors, Indices };

    QRhiBuffer* upload(std::uint64_t id, Slot slot, const void* data, std::size_t bytes,
                       QRhiResourceUpdateBatch* updates);
    bool createUniforms();

    QRhi* m_rhi;
    Owned<QRhiBuffer> m_litUniforms;
    Owned<QRhiBuffer> m_unlitUniforms;
    Owned<QRhiShaderResourceBindings> m_litBindings;
    Owned<QRhiShaderResourceBindings> m_unlitBindings;
    Owned<QRhiGraphicsPipeline> m_pipeline;
    std::unordered_map<std::uint64_t, Gpu> m_buffers;
    std::vector<Call> m_calls;
};

} // namespace kalve137::render
