// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/edge_quad.h"
#include "render/rhi/rhi_backend.h"

#include <QFile>
#include <QString>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace kalve137::render::rhi_backend_detail {

inline constexpr quint32 kVertexStride = sizeof(Vertex);
inline constexpr quint32 kPositionOffset = offsetof(Vertex, position);
inline constexpr quint32 kPackedOffset = offsetof(Vertex, packed);
inline constexpr quint32 kSegmentStride = sizeof(Segment);
inline constexpr quint32 kStartOffset = offsetof(Segment, start);
inline constexpr quint32 kEndOffset = offsetof(Segment, end);
inline constexpr quint32 kSegmentPackedOffset = offsetof(Segment, packed);
inline constexpr int kEntryBytes = sizeof(Entry);
inline constexpr int kCulled = 1;
inline constexpr int kBiased = 2;

inline constexpr std::array<float, 2 * static_cast<std::size_t>(kEdgeCorners)> kCorners{0.0F, -1.0F, 0.0F, 1.0F,
                                                                                        1.0F, -1.0F, 1.0F, 1.0F};
inline constexpr quint32 kCornerStride = 2 * sizeof(float);

struct Uniforms {
    std::array<float, 16> viewProjection{};
    std::array<float, 4> eye{};
    std::array<float, 4> eyePosition{};
    std::array<float, 4> light{};
    std::array<float, 4> look{};
    std::array<float, 4> specular{};
    std::array<float, 4> options{};
    std::array<float, 4> edge{};
    std::array<float, 4> target{};
    std::array<float, 4> gold{};
    std::array<float, 4> steps{};
    std::array<float, 4> hidden{};
    std::array<float, 16> wideViewProjection{};
    std::array<float, 4> overlay{};
    std::array<float, 4> overlayBias{};
    std::array<float, 4> overlayScale{};
    std::array<float, 4> overlayPlane{};
};

inline constexpr quint32 kPositionStride = sizeof(Position);
inline constexpr quint32 kOverlayLineStride = sizeof(OverlayLine);
inline constexpr quint32 kOverlayVertexStride = sizeof(OverlayVertex);
inline constexpr quint32 kDatumLineStride = sizeof(DatumLine);
inline constexpr quint32 kMarkerStride = sizeof(Marker);
inline constexpr float kWindowDepthUnit = 1.0F / static_cast<float>(1 << 24);
inline constexpr int kPostCorners = 3;

struct PickUniforms {
    std::array<std::uint32_t, 4> pick{};
    std::array<float, 4> widths{};
};

inline constexpr std::array kIdKinds{PickKind::Face, PickKind::Edge, PickKind::Point};

inline std::size_t idIndexOf(PickKind kind)
{
    return static_cast<std::size_t>(kind) - 1;
}

inline QShader loadShader(const QString& name)
{
    QFile file(QStringLiteral(":/kalve137/shaders/%1.qsb").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

inline int paletteRows(std::size_t entries)
{
    return static_cast<int>(std::max<std::size_t>(1, (entries + kPaletteWidth - 1) / kPaletteWidth));
}

inline bool fitsBuffer(std::size_t bytes)
{
    return bytes <= std::numeric_limits<quint32>::max();
}

} // namespace kalve137::render::rhi_backend_detail
