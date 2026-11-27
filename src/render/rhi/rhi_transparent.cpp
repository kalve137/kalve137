// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_backend.h"

#include "render/edge_quad.h"
#include "render/rhi/rhi_backend_detail.h"

#include <QColor>
#include <rhi/qrhi.h>

#include <array>
#include <cstddef>

namespace kalve137::render {

using namespace rhi_backend_detail;

bool RhiBackend::createTransparentPipelines(QRhiRenderPassDescriptor* pass, int samples)
{
    if (m_transparentLayout == nullptr) {
        m_transparentLayout = bindingsFor(m_layoutTexture.get());
        if (m_transparentLayout == nullptr) {
            return false;
        }
    }
    if (m_transparentEdgeLayout == nullptr) {
        m_transparentEdgeLayout = bindingsFor(m_layoutTexture.get());
        if (m_transparentEdgeLayout == nullptr) {
            return false;
        }
    }
    m_transparentEdges = edgePipeline(pass, samples, true);
    if (m_transparentEdges == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < m_transparentFaces.size(); ++i) {
        const auto variant = static_cast<int>(i);
        m_transparentFaces.at(i) =
            facePipeline(pass, samples, (variant & kCulled) != 0, (variant & kBiased) != 0, true);
        if (m_transparentFaces.at(i) == nullptr) {
            return false;
        }
    }
    return true;
}

void RhiBackend::prepareTransparent(const DrawList& list, const std::vector<const Gpu*>& placed)
{
    m_transparentCalls.clear();
    m_transparentFacesShown = list.faces;
    m_transparentEdgesShown = list.edges;
    for (const TransparentDraw& draw : list.transparent) {
        if (draw.chunk >= list.chunks.size() || draw.chunk >= placed.size() || placed[draw.chunk] == nullptr) {
            continue;
        }
        const Chunk& chunk = list.chunks[draw.chunk];
        if (draw.range >= chunk.ranges.size()) {
            continue;
        }
        const Gpu* gpu = placed[draw.chunk];
        const BodyRange& range = chunk.ranges[draw.range];
        if (range.firstIndex + range.indexCount > gpu->indexCount ||
            range.firstSegment + range.segmentCount > gpu->segmentCount) {
            continue;
        }
        m_transparentCalls.push_back(
            {.gpu = gpu, .range = range, .opacity = chunk.opacity, .cull = list.cullClosed && chunk.closed});
    }
}

void RhiBackend::drawTransparent(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const
{
    for (const TransparentCall& call : m_transparentCalls) {
        if (m_transparentFacesShown && call.range.indexCount > 0) {
            const int index = (call.cull ? kCulled : 0) + (m_biased ? kBiased : 0);
            buffer->setGraphicsPipeline(m_transparentFaces.at(static_cast<std::size_t>(index)).get());
            buffer->setViewport(viewport);
            buffer->setBlendConstants(QColor::fromRgbF(call.opacity, call.opacity, call.opacity, call.opacity));
            buffer->setShaderResources(call.gpu->faces.bindings.get());
            const QRhiCommandBuffer::VertexInput input(call.gpu->vertices.get(), 0);
            buffer->setVertexInput(0, 1, &input, call.gpu->indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
            buffer->drawIndexed(call.range.indexCount, 1, call.range.firstIndex);
        }
        if (m_transparentEdgesShown && call.range.segmentCount > 0) {
            buffer->setGraphicsPipeline(m_transparentEdges.get());
            buffer->setViewport(viewport);
            buffer->setShaderResources(call.gpu->edges.bindings.get());
            const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{
                QRhiCommandBuffer::VertexInput(m_corners.get(), 0),
                QRhiCommandBuffer::VertexInput(call.gpu->segments.get(), call.range.firstSegment * kSegmentStride)};
            buffer->setVertexInput(0, static_cast<int>(inputs.size()), inputs.data());
            buffer->draw(kEdgeCorners, call.range.segmentCount);
        }
    }
}

} // namespace kalve137::render
