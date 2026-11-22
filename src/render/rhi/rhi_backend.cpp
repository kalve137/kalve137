// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_backend.h"

#include "log/log.h"

#include <QByteArray>
#include <QFile>
#include <QMatrix4x4>
#include <QString>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

namespace kalve137::render {

namespace {

constexpr quint32 kVertexStride = sizeof(Vertex);
constexpr quint32 kPositionOffset = offsetof(Vertex, position);
constexpr quint32 kPackedOffset = offsetof(Vertex, packed);
constexpr int kEntryBytes = sizeof(Entry);

struct Uniforms {
    std::array<float, 16> viewProjection{};
    std::array<float, 4> eye{};
    std::array<float, 4> eyePosition{};
    std::array<float, 4> light{};
    std::array<float, 4> look{};
    std::array<float, 4> specular{};
    std::array<float, 4> options{};
};

QShader loadShader(const QString& name)
{
    QFile file(QStringLiteral(":/kalve137/shaders/%1.qsb").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

int paletteRows(std::size_t entries)
{
    return static_cast<int>(std::max<std::size_t>(1, (entries + kPaletteWidth - 1) / kPaletteWidth));
}

} // namespace

void RhiBackend::Release::operator()(QRhiResource* resource) const
{
    if (resource != nullptr) {
        resource->deleteLater();
    }
}

RhiBackend::RhiBackend(QRhi* rhi) : m_rhi(rhi) {}

RhiBackend::~RhiBackend() = default;

RhiBackend::Owned<QRhiShaderResourceBindings> RhiBackend::bindingsFor(QRhiTexture* palette) const
{
    Owned<QRhiShaderResourceBindings> bindings(m_rhi->newShaderResourceBindings());
    bindings->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(
             0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_uniforms.get()),
         QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::VertexStage, palette,
                                                   m_sampler.get())});
    return bindings->create() ? std::move(bindings) : nullptr;
}

bool RhiBackend::createShared()
{
    if (m_layout != nullptr) {
        return true;
    }
    m_uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_layoutTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    if (!m_uniforms->create() || !m_sampler->create() || !m_layoutTexture->create()) {
        return false;
    }
    m_layout = bindingsFor(m_layoutTexture.get());
    return m_layout != nullptr;
}

RhiBackend::Owned<QRhiGraphicsPipeline> RhiBackend::pipeline(QRhiRenderPassDescriptor* pass, int samples,
                                                             bool cull) const
{
    const QShader vertex = loadShader(QStringLiteral("body.vert"));
    const QShader fragment = loadShader(QStringLiteral("body.frag"));
    if (!vertex.isValid() || !fragment.isValid()) {
        K137_ERROR(Viewer, "The new view could not load its body shaders");
        return nullptr;
    }
    Owned<QRhiGraphicsPipeline> result(m_rhi->newGraphicsPipeline());
    result->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
    QRhiVertexInputLayout layout;
    layout.setBindings({QRhiVertexInputBinding(kVertexStride)});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, kPositionOffset},
                          {0, 1, QRhiVertexInputAttribute::UNormByte4, kPackedOffset}});
    result->setVertexInputLayout(layout);
    result->setTopology(QRhiGraphicsPipeline::Triangles);
    result->setFrontFace(QRhiGraphicsPipeline::CCW);
    result->setCullMode(cull ? QRhiGraphicsPipeline::Back : QRhiGraphicsPipeline::None);
    result->setDepthTest(true);
    result->setDepthWrite(true);
    result->setDepthOp(QRhiGraphicsPipeline::Less);
    result->setSampleCount(samples);
    result->setShaderResourceBindings(m_layout.get());
    result->setRenderPassDescriptor(pass);
    if (!result->create()) {
        K137_ERROR(Viewer, "The new view could not create its body pipeline at {}x samples", samples);
        return nullptr;
    }
    return result;
}

bool RhiBackend::setTarget(QRhiRenderPassDescriptor* pass, int samples)
{
    m_culled.reset();
    m_unculled.reset();
    if (!createShared()) {
        K137_ERROR(Viewer, "The new view could not create its shared GPU resources");
        return false;
    }
    m_culled = pipeline(pass, samples, true);
    m_unculled = pipeline(pass, samples, false);
    return m_culled != nullptr && m_unculled != nullptr;
}

bool RhiBackend::refreshMesh(const Chunk& chunk, Gpu& gpu, QRhiResourceUpdateBatch* updates)
{
    const std::size_t vertexBytes = chunk.vertices.size_bytes();
    const std::size_t indexBytes = chunk.indices.size_bytes();
    if (vertexBytes == 0 || vertexBytes > std::numeric_limits<quint32>::max() ||
        indexBytes > std::numeric_limits<quint32>::max()) {
        return false;
    }
    gpu.meshRevision = 0;
    gpu.vertices.reset(
        m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, static_cast<quint32>(vertexBytes)));
    gpu.indices.reset(
        m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, static_cast<quint32>(indexBytes)));
    if (!gpu.vertices->create() || !gpu.indices->create()) {
        return false;
    }
    updates->uploadStaticBuffer(gpu.vertices.get(), chunk.vertices.data());
    updates->uploadStaticBuffer(gpu.indices.get(), chunk.indices.data());
    gpu.indexCount = static_cast<std::uint32_t>(chunk.indices.size());
    gpu.meshRevision = chunk.meshRevision;
    ++m_meshUploads;
    return true;
}

bool RhiBackend::refreshPalette(const Chunk& chunk, Gpu& gpu, QRhiResourceUpdateBatch* updates)
{
    const int rows = paletteRows(chunk.palette.size());
    if (gpu.palette == nullptr || gpu.palette->pixelSize().height() != rows) {
        gpu.paletteRevision = 0;
        gpu.bindings.reset();
        gpu.palette.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(static_cast<int>(kPaletteWidth), rows)));
        if (!gpu.palette->create()) {
            return false;
        }
        gpu.bindings = bindingsFor(gpu.palette.get());
        if (gpu.bindings == nullptr) {
            return false;
        }
    }
    QByteArray texels(static_cast<qsizetype>(kPaletteWidth) * rows * kEntryBytes, '\0');
    std::memcpy(texels.data(), chunk.palette.data(), chunk.palette.size_bytes());
    updates->uploadTexture(gpu.palette.get(),
                           QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(texels)));
    gpu.paletteRevision = chunk.paletteRevision;
    ++m_paletteUploads;
    return true;
}

bool RhiBackend::refresh(const Chunk& chunk, Gpu& gpu, QRhiResourceUpdateBatch* updates)
{
    if (chunk.palette.size() > kPaletteEntries) {
        return false;
    }
    if (gpu.meshRevision != chunk.meshRevision && !refreshMesh(chunk, gpu, updates)) {
        return false;
    }
    return gpu.paletteRevision == chunk.paletteRevision || refreshPalette(chunk, gpu, updates);
}

bool RhiBackend::prepare(const DrawList& list, QRhiResourceUpdateBatch* updates)
{
    m_calls.clear();
    if (m_culled == nullptr || m_unculled == nullptr) {
        return false;
    }
    const Frame& frame = list.frame;
    QMatrix4x4 viewProjection;
    std::memcpy(viewProjection.data(), frame.viewProjection.data(), sizeof(frame.viewProjection));
    const QMatrix4x4 clip = m_rhi->clipSpaceCorrMatrix() * viewProjection;
    Uniforms uniforms;
    std::memcpy(uniforms.viewProjection.data(), clip.constData(), sizeof(uniforms.viewProjection));
    const Look& look = frame.look;
    uniforms.eye = {frame.eye[0], frame.eye[1], frame.eye[2], 0.0F};
    uniforms.eyePosition = {frame.eyePosition[0], frame.eyePosition[1], frame.eyePosition[2],
                            frame.perspective ? 1.0F : 0.0F};
    uniforms.light = {frame.light[0], frame.light[1], frame.light[2], 0.0F};
    uniforms.look = {look.ambient, look.diffuse, look.shininess, 0.0F};
    uniforms.specular = {look.specular[0], look.specular[1], look.specular[2], 0.0F};
    uniforms.options = {list.lit ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F};
    updates->updateDynamicBuffer(m_uniforms.get(), 0, sizeof(Uniforms), &uniforms);

    for (auto& [id, gpu] : m_chunks) {
        gpu.used = false;
    }
    bool complete = true;
    for (const Chunk& chunk : list.chunks) {
        if (chunk.indices.empty()) {
            continue;
        }
        Gpu& gpu = m_chunks[chunk.id];
        if (!refresh(chunk, gpu, updates)) {
            complete = false;
            continue;
        }
        gpu.used = true;
        m_calls.push_back({.gpu = &gpu, .cull = list.cullClosed && chunk.closed});
    }
    std::erase_if(m_chunks, [](const auto& item) { return !item.second.used; });
    std::ranges::stable_partition(m_calls, [](const Call& call) { return call.cull; });
    if (!complete) {
        K137_ERROR(Viewer, "The new view could not create the GPU resources for every chunk");
    }
    return complete;
}

void RhiBackend::draw(QRhiCommandBuffer* buffer, QSize size) const
{
    const QRhiViewport viewport(0, 0, static_cast<float>(size.width()), static_cast<float>(size.height()));
    const QRhiGraphicsPipeline* bound = nullptr;
    for (const Call& call : m_calls) {
        QRhiGraphicsPipeline* pipeline = call.cull ? m_culled.get() : m_unculled.get();
        if (pipeline != bound) {
            buffer->setGraphicsPipeline(pipeline);
            buffer->setViewport(viewport);
            bound = pipeline;
        }
        buffer->setShaderResources(call.gpu->bindings.get());
        const QRhiCommandBuffer::VertexInput input(call.gpu->vertices.get(), 0);
        buffer->setVertexInput(0, 1, &input, call.gpu->indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
        buffer->drawIndexed(call.gpu->indexCount);
    }
}

} // namespace kalve137::render
