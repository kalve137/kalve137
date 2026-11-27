// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/cpu_backend.h"
#include "render/edge_quad.h"
#include "render/style.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace kalve137::render::cpu_backend_detail {

inline constexpr int kTilePixels = 32;
inline constexpr int kChunksPerThread = 4;
inline constexpr int kSubpixelBits = 8;
inline constexpr std::int64_t kSubpixel = 1 << kSubpixelBits;
inline constexpr double kGuard = 1 << 21;
inline constexpr float kByte = 255.0F;
inline constexpr std::uint32_t kOpaque = 0xFF000000U;
inline constexpr float kDepthUnit = 2.0F / static_cast<float>(1 << 24);

inline std::uint32_t channel(float value)
{
    return static_cast<std::uint32_t>(std::lround(std::min(kByte, std::max(0.0F, value))));
}

inline std::uint32_t pack(float red, float green, float blue)
{
    return kOpaque | (channel(red) << 16) | (channel(green) << 8) | channel(blue);
}

inline std::int32_t fixed(double samples)
{
    return static_cast<std::int32_t>(std::floor((std::clamp(samples, -kGuard, kGuard) * kSubpixel) + 0.5));
}

inline int defaultThreads(int threads)
{
    return threads > 0 ? threads : std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

inline std::size_t chunkStart(std::size_t count, int chunk, int chunks)
{
    return count * static_cast<std::size_t>(chunk) / static_cast<std::size_t>(chunks);
}

inline constexpr int kEncodeSteps = 1 << 14;

inline const std::array<std::uint8_t, kEncodeSteps + 1>& encodeTable()
{
    static const std::array<std::uint8_t, kEncodeSteps + 1> kTable = [] {
        std::array<std::uint8_t, kEncodeSteps + 1> values{};
        for (int i = 0; i <= kEncodeSteps; ++i) {
            values[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(
                std::lround(toSrgb(static_cast<float>(i) / static_cast<float>(kEncodeSteps)) * kByte));
        }
        return values;
    }();
    return kTable;
}

inline std::uint32_t encoded(float linear)
{
    const float step = std::clamp(linear, 0.0F, 1.0F) * static_cast<float>(kEncodeSteps);
    return encodeTable()[static_cast<std::size_t>(std::lround(step))];
}

inline std::array<float, 3> interpolated(const std::array<float, 3>& base, const std::array<float, 3>& d1,
                                         const std::array<float, 3>& d2, float f1, float f2)
{
    return {base[0] + (d1[0] * f1) + (d2[0] * f2), base[1] + (d1[1] * f1) + (d2[1] * f2),
            base[2] + (d1[2] * f1) + (d2[2] * f2)};
}

inline std::array<float, 3> difference(const std::array<float, 3>& to, const std::array<float, 3>& from, float scale)
{
    return {(to[0] - from[0]) * scale, (to[1] - from[1]) * scale, (to[2] - from[2]) * scale};
}

inline Normal unit(const Normal& vector)
{
    const float length = std::sqrt((vector[0] * vector[0]) + (vector[1] * vector[1]) + (vector[2] * vector[2]));
    return length > 0.0F ? Normal{vector[0] / length, vector[1] / length, vector[2] / length} : vector;
}

inline float haloStrength(float across, float along, float span, float corePixels, float haloPixels)
{
    const float past = std::max({-along, along - span, 0.0F});
    const float outside = std::max(std::hypot(across, past) - corePixels, 0.0F);
    return kHaloStep * std::clamp(1.0F - (outside / std::max(haloPixels, 1e-3F)), 0.0F, 1.0F);
}

inline bool dashedOff(int pixel, float dash, float gap)
{
    return std::fmod(static_cast<float>(pixel), dash + gap) >= dash;
}

inline Clip wideClip(const Frame& frame, const Position& position, const Normal& shift)
{
    const std::array<float, 16>& m = frame.wideViewProjection;
    const double x = static_cast<double>(position[0]) + shift[0];
    const double y = static_cast<double>(position[1]) + shift[1];
    const double z = static_cast<double>(position[2]) + shift[2];
    Clip clip{};
    for (std::size_t r = 0; r < clip.size(); ++r) {
        clip.at(r) = (static_cast<double>(m.at(r)) * x) + (static_cast<double>(m.at(r + 4)) * y) +
                     (static_cast<double>(m.at(r + 8)) * z) + static_cast<double>(m.at(r + 12));
    }
    return clip;
}

inline Clip lerp(const Clip& from, const Clip& to, double t)
{
    Clip result{};
    for (std::size_t r = 0; r < result.size(); ++r) {
        result.at(r) = from.at(r) + ((to.at(r) - from.at(r)) * t);
    }
    return result;
}

} // namespace kalve137::render::cpu_backend_detail
