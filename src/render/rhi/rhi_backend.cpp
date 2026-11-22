// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_backend.h"

#include "log/log.h"

#include <QFile>
#include <QMatrix4x4>
#include <QString>
#include <rhi/qrhi.h>

#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace kalve137::render {

namespace {

constexpr int kSlots = 4;
constexpr quint32 kPositionStride = sizeof(Position);
constexpr quint32 kNormalStride = sizeof(Normal);
constexpr quint32 kColorStride = sizeof(VertexColor);

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

template <typename T>
std::size_t bytesOf(const Stream<T>& stream)
{
    return stream.data.size_bytes();
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

bool RhiBackend::createUniforms()
{
    if (m_litBindings != nullptr) {
        return true;
    }
    m_litUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
    m_unlitUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
    if (!m_litUniforms->create() || !m_unlitUniforms->create()) {
        return false;
    }
    const auto bindings = [this](QRhiBuffer* uniforms) {
        Owned<QRhiShaderResourceBindings> result(m_rhi->newShaderResourceBindings());
        result->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniforms)});
        return result->create() ? std::move(result) : nullptr;
    };
    m_litBindings = bindings(m_litUniforms.get());
    m_unlitBindings = bindings(m_unlitUniforms.get());
    return m_litBindings != nullptr && m_unlitBindings != nullptr;
}

bool RhiBackend::setTarget(QRhiRenderPassDescriptor* pass, int samples)
{
    m_pipeline.reset();
    if (!createUniforms()) {
        K137_ERROR(Viewer, "The new view could not create its uniform buffers");
        return false;
    }
    const QShader vertex = loadShader(QStringLiteral("body.vert"));
    const QShader fragment = loadShader(QStringLiteral("body.frag"));
    if (!vertex.isValid() || !fragment.isValid()) {
        K137_ERROR(Viewer, "The new view could not load its body shaders");
        return false;
    }
    Owned<QRhiGraphicsPipeline> pipeline(m_rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
    QRhiVertexInputLayout layout;
    layout.setBindings({QRhiVertexInputBinding(kPositionStride), QRhiVertexInputBinding(kNormalStride),
                        QRhiVertexInputBinding(kColorStride)});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                          {1, 1, QRhiVertexInputAttribute::Float3, 0},
                          {2, 2, QRhiVertexInputAttribute::UNormByte4, 0}});
    pipeline->setVertexInputLayout(layout);
    pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    pipeline->setCullMode(QRhiGraphicsPipeline::None);
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(true);
    pipeline->setDepthOp(QRhiGraphicsPipeline::Less);
    pipeline->setSampleCount(samples);
    pipeline->setShaderResourceBindings(m_litBindings.get());
    pipeline->setRenderPassDescriptor(pass);
    if (!pipeline->create()) {
        K137_ERROR(Viewer, "The new view could not create its body pipeline at {}x samples", samples);
        return false;
    }
    m_pipeline = std::move(pipeline);
    return true;
}

QRhiBuffer* RhiBackend::upload(std::uint64_t id, Slot slot, const void* data, std::size_t bytes,
                               QRhiResourceUpdateBatch* updates)
{
    const std::uint64_t key = (id * kSlots) + static_cast<std::uint64_t>(slot);
    auto [it, added] = m_buffers.try_emplace(key);
    Gpu& gpu = it->second;
    gpu.used = true;
    if (!added) {
        return gpu.buffer.get();
    }
    if (bytes == 0 || bytes > std::numeric_limits<quint32>::max()) {
        m_buffers.erase(it);
        return nullptr;
    }
    const QRhiBuffer::UsageFlags usage = slot == Slot::Indices ? QRhiBuffer::IndexBuffer : QRhiBuffer::VertexBuffer;
    gpu.buffer.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, usage, static_cast<quint32>(bytes)));
    if (!gpu.buffer->create()) {
        m_buffers.erase(it);
        return nullptr;
    }
    updates->uploadStaticBuffer(gpu.buffer.get(), data);
    return gpu.buffer.get();
}

bool RhiBackend::prepare(const DrawList& list, QRhiResourceUpdateBatch* updates)
{
    m_calls.clear();
    if (m_pipeline == nullptr) {
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
    uniforms.options = {1.0F, 0.0F, 0.0F, 0.0F};
    updates->updateDynamicBuffer(m_litUniforms.get(), 0, sizeof(Uniforms), &uniforms);
    uniforms.options[0] = 0.0F;
    updates->updateDynamicBuffer(m_unlitUniforms.get(), 0, sizeof(Uniforms), &uniforms);

    for (auto& [key, gpu] : m_buffers) {
        gpu.used = false;
    }
    bool complete = true;
    for (const Batch& batch : list.batches) {
        if (batch.kind != ItemKind::Faces || batch.triangles.data.empty()) {
            continue;
        }
        const std::size_t indexCount = batch.triangles.data.size() * 3;
        if (indexCount > std::numeric_limits<std::uint32_t>::max()) {
            complete = false;
            continue;
        }
        Call call{
            .positions = upload(batch.positions.id, Slot::Positions, batch.positions.data.data(),
                                bytesOf(batch.positions), updates),
            .normals =
                upload(batch.normals.id, Slot::Normals, batch.normals.data.data(), bytesOf(batch.normals), updates),
            .colors = upload(batch.colors.id, Slot::Colors, batch.colors.data.data(), bytesOf(batch.colors), updates),
            .indices = upload(batch.triangles.id, Slot::Indices, batch.triangles.data.data(), bytesOf(batch.triangles),
                              updates),
            .indexCount = static_cast<std::uint32_t>(indexCount),
            .lit = batch.lit,
        };
        if (call.positions == nullptr || call.normals == nullptr || call.colors == nullptr || call.indices == nullptr) {
            complete = false;
            continue;
        }
        m_calls.push_back(call);
    }
    std::erase_if(m_buffers, [](const auto& item) { return !item.second.used; });
    if (!complete) {
        K137_ERROR(Viewer, "The new view could not create the GPU buffers for every body");
    }
    return complete;
}

void RhiBackend::draw(QRhiCommandBuffer* buffer, QSize size) const
{
    if (m_pipeline == nullptr || m_calls.empty()) {
        return;
    }
    buffer->setGraphicsPipeline(m_pipeline.get());
    buffer->setViewport(QRhiViewport(0, 0, static_cast<float>(size.width()), static_cast<float>(size.height())));
    QRhiShaderResourceBindings* bound = nullptr;
    for (const Call& call : m_calls) {
        QRhiShaderResourceBindings* bindings = call.lit ? m_litBindings.get() : m_unlitBindings.get();
        if (bindings != bound) {
            buffer->setShaderResources(bindings);
            bound = bindings;
        }
        const std::array<QRhiCommandBuffer::VertexInput, 3> inputs{QRhiCommandBuffer::VertexInput(call.positions, 0),
                                                                   QRhiCommandBuffer::VertexInput(call.normals, 0),
                                                                   QRhiCommandBuffer::VertexInput(call.colors, 0)};
        buffer->setVertexInput(0, static_cast<int>(inputs.size()), inputs.data(), call.indices, 0,
                               QRhiCommandBuffer::IndexUInt32);
        buffer->drawIndexed(call.indexCount);
    }
}

} // namespace kalve137::render
