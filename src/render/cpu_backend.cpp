// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/cpu_backend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <thread>

namespace kalve137::render {

namespace {

constexpr int kTilePixels = 32;
constexpr int kChunksPerThread = 4;
constexpr int kSubpixelBits = 8;
constexpr std::int64_t kSubpixel = 1 << kSubpixelBits;
constexpr double kGuard = 1 << 21;
constexpr float kByte = 255.0F;
constexpr std::uint32_t kOpaque = 0xFF000000U;

std::uint32_t channel(float value)
{
    return static_cast<std::uint32_t>(std::lround(std::min(kByte, std::max(0.0F, value))));
}

std::uint32_t pack(float red, float green, float blue)
{
    return kOpaque | (channel(red) << 16) | (channel(green) << 8) | channel(blue);
}

std::int32_t fixed(double samples)
{
    return static_cast<std::int32_t>(std::floor((std::clamp(samples, -kGuard, kGuard) * kSubpixel) + 0.5));
}

int defaultThreads(int threads)
{
    return threads > 0 ? threads : std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

std::size_t chunkStart(std::size_t count, int chunk, int chunks)
{
    return count * static_cast<std::size_t>(chunk) / static_cast<std::size_t>(chunks);
}

constexpr int kEncodeSteps = 1 << 14;

const std::array<std::uint8_t, kEncodeSteps + 1>& encodeTable()
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

std::uint32_t encoded(float linear)
{
    const float step = std::clamp(linear, 0.0F, 1.0F) * static_cast<float>(kEncodeSteps);
    return encodeTable()[static_cast<std::size_t>(std::lround(step))];
}

std::array<float, 3> interpolated(const std::array<float, 3>& base, const std::array<float, 3>& d1,
                                  const std::array<float, 3>& d2, float f1, float f2)
{
    return {base[0] + (d1[0] * f1) + (d2[0] * f2), base[1] + (d1[1] * f1) + (d2[1] * f2),
            base[2] + (d1[2] * f1) + (d2[2] * f2)};
}

std::array<float, 3> difference(const std::array<float, 3>& to, const std::array<float, 3>& from, float scale)
{
    return {(to[0] - from[0]) * scale, (to[1] - from[1]) * scale, (to[2] - from[2]) * scale};
}

Normal unit(const Normal& vector)
{
    const float length = std::sqrt((vector[0] * vector[0]) + (vector[1] * vector[1]) + (vector[2] * vector[2]));
    return length > 0.0F ? Normal{vector[0] / length, vector[1] / length, vector[2] / length} : vector;
}

} // namespace

CpuBackend::CpuBackend(int threads) : m_pool(defaultThreads(threads)) {}

Report CpuBackend::draw(const DrawList& list, int supersample, Image& image)
{
    Report report;
    for (const Batch& batch : list.batches) {
        if (batch.kind != ItemKind::Faces && std::ranges::find(report.skipped, batch.kind) == report.skipped.end()) {
            report.skipped.push_back(batch.kind);
        }
    }
    const Frame& frame = list.frame;
    m_frame = frame;
    m_width = std::max(0, frame.width);
    m_height = std::max(0, frame.height);
    image.width = m_width;
    image.height = m_height;
    image.pixels.resize(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height));
    if (m_width == 0 || m_height == 0) {
        return report;
    }
    m_supersample = std::max(1, supersample);
    m_tileSamples = kTilePixels * m_supersample;
    m_tilesX = (m_width + kTilePixels - 1) / kTilePixels;
    m_tilesY = (m_height + kTilePixels - 1) / kTilePixels;
    const snapshot::Color& background = frame.background;
    m_background = pack(background.red * kByte, background.green * kByte, background.blue * kByte);
    const std::size_t tileArea = static_cast<std::size_t>(m_tileSamples) * static_cast<std::size_t>(m_tileSamples);
    m_tileBuffers.resize(static_cast<std::size_t>(m_pool.size()));
    for (TileBuffers& buffers : m_tileBuffers) {
        buffers.color.resize(tileArea);
        buffers.depth.resize(tileArea);
    }
    project(list);
    bin();
    m_pool.run(m_tilesX * m_tilesY, [&](int tile, int worker) { shadeTile(tile, worker, image); });
    return report;
}

void CpuBackend::project(const DrawList& list)
{
    const Frame& frame = list.frame;
    const std::array<float, 16>& m = frame.viewProjection;
    const double width = static_cast<double>(m_width) * m_supersample;
    const double height = static_cast<double>(m_height) * m_supersample;
    const int chunks = m_pool.size() * kChunksPerThread;
    m_vertices.clear();
    m_triangles.clear();
    for (const Batch& batch : list.batches) {
        if (batch.kind != ItemKind::Faces) {
            continue;
        }
        const std::span<const Position> positions = batch.positions.data;
        const std::span<const Normal> normals = batch.normals.data;
        const auto offset = static_cast<std::uint32_t>(m_vertices.size());
        m_vertices.resize(m_vertices.size() + positions.size());
        m_pool.run(chunks, [&](int chunk, int) {
            const std::size_t last = chunkStart(positions.size(), chunk + 1, chunks);
            for (std::size_t i = chunkStart(positions.size(), chunk, chunks); i < last; ++i) {
                const Position& p = positions[i];
                const auto row = [&](int r) {
                    const auto index = static_cast<std::size_t>(r);
                    return (static_cast<double>(m[index]) * p[0]) + (static_cast<double>(m[index + 4]) * p[1]) +
                           (static_cast<double>(m[index + 8]) * p[2]) + static_cast<double>(m[index + 12]);
                };
                const double w = row(3);
                Projected& out = m_vertices[offset + i];
                out.valid = w > 0.0;
                if (!out.valid) {
                    continue;
                }
                out.x = fixed((row(0) / w + 1.0) * 0.5 * width);
                out.y = fixed((1.0 - row(1) / w) * 0.5 * height);
                out.z = static_cast<float>(row(2) / w);
                out.normal = i < normals.size() ? normals[i] : Normal{0.0F, 0.0F, 1.0F};
                out.position = p;
            }
        });
        const std::span<const VertexColor> colors = batch.colors.data;
        for (const snapshot::Triangle& triangle : batch.triangles.data) {
            const VertexColor color = triangle[0] < colors.size() ? colors[triangle[0]] : VertexColor{};
            m_triangles.push_back({.a = offset + triangle[0],
                                   .b = offset + triangle[1],
                                   .c = offset + triangle[2],
                                   .color = color,
                                   .linear = linearOf(color),
                                   .lit = batch.lit});
        }
    }
}

void CpuBackend::bin()
{
    const int tiles = m_tilesX * m_tilesY;
    const int samplesX = m_width * m_supersample;
    const int samplesY = m_height * m_supersample;
    const int tileSamples = m_tileSamples;
    const int chunks = m_pool.size() * kChunksPerThread;
    const std::size_t count = m_triangles.size();
    Bins& bins = m_bins;
    bins.entries.resize(static_cast<std::size_t>(chunks));
    bins.counts.resize(static_cast<std::size_t>(chunks));
    m_pool.run(chunks, [&](int chunk, int) {
        auto& entries = bins.entries[static_cast<std::size_t>(chunk)];
        auto& counts = bins.counts[static_cast<std::size_t>(chunk)];
        entries.clear();
        counts.assign(static_cast<std::size_t>(tiles), 0);
        const std::size_t last = chunkStart(count, chunk + 1, chunks);
        for (std::size_t i = chunkStart(count, chunk, chunks); i < last; ++i) {
            const Projected& a = m_vertices[m_triangles[i].a];
            const Projected& b = m_vertices[m_triangles[i].b];
            const Projected& c = m_vertices[m_triangles[i].c];
            if (!a.valid || !b.valid || !c.valid) {
                continue;
            }
            const std::int64_t area = (static_cast<std::int64_t>(b.x - a.x) * (c.y - a.y)) -
                                      (static_cast<std::int64_t>(b.y - a.y) * (c.x - a.x));
            if (area == 0) {
                continue;
            }
            const int minX = std::min({a.x, b.x, c.x}) >> kSubpixelBits;
            const int maxX = std::max({a.x, b.x, c.x}) >> kSubpixelBits;
            const int minY = std::min({a.y, b.y, c.y}) >> kSubpixelBits;
            const int maxY = std::max({a.y, b.y, c.y}) >> kSubpixelBits;
            if (maxX < 0 || maxY < 0 || minX >= samplesX || minY >= samplesY) {
                continue;
            }
            const int tx0 = std::max(0, minX) / tileSamples;
            const int ty0 = std::max(0, minY) / tileSamples;
            const int tx1 = std::min(samplesX - 1, maxX) / tileSamples;
            const int ty1 = std::min(samplesY - 1, maxY) / tileSamples;
            for (int ty = ty0; ty <= ty1; ++ty) {
                for (int tx = tx0; tx <= tx1; ++tx) {
                    const auto tile = static_cast<std::uint32_t>((ty * m_tilesX) + tx);
                    entries.emplace_back(tile, static_cast<std::uint32_t>(i));
                    ++counts[tile];
                }
            }
        }
    });
    bins.start.assign(static_cast<std::size_t>(tiles) + 1, 0);
    std::uint32_t total = 0;
    for (int tile = 0; tile < tiles; ++tile) {
        bins.start[static_cast<std::size_t>(tile)] = total;
        for (auto& counts : bins.counts) {
            const std::uint32_t here = counts[static_cast<std::size_t>(tile)];
            counts[static_cast<std::size_t>(tile)] = total;
            total += here;
        }
    }
    bins.start[static_cast<std::size_t>(tiles)] = total;
    bins.items.resize(total);
    m_pool.run(chunks, [&](int chunk, int) {
        auto& counts = bins.counts[static_cast<std::size_t>(chunk)];
        for (const auto& [tile, item] : bins.entries[static_cast<std::size_t>(chunk)]) {
            bins.items[counts[tile]++] = item;
        }
    });
}

void CpuBackend::shadeTile(int tile, int worker, Image& image)
{
    const int supersample = m_supersample;
    const int size = m_tileSamples;
    const int samplesX = m_width * supersample;
    const int samplesY = m_height * supersample;
    const int sx0 = (tile % m_tilesX) * size;
    const int sy0 = (tile / m_tilesX) * size;
    const int sx1 = std::min(sx0 + size, samplesX);
    const int sy1 = std::min(sy0 + size, samplesY);
    TileBuffers& buffers = m_tileBuffers[static_cast<std::size_t>(worker)];
    std::uint32_t* color = buffers.color.data();
    float* depth = buffers.depth.data();
    for (int sy = sy0; sy < sy1; ++sy) {
        const std::size_t row = static_cast<std::size_t>(sy - sy0) * static_cast<std::size_t>(size);
        std::fill_n(color + row, size, m_background);
        std::fill_n(depth + row, size, std::numeric_limits<float>::infinity());
    }

    for (std::uint32_t k = m_bins.start[static_cast<std::size_t>(tile)];
         k < m_bins.start[static_cast<std::size_t>(tile) + 1]; ++k) {
        const Triangle& triangle = m_triangles[m_bins.items[k]];
        const Projected& a = m_vertices[triangle.a];
        Projected b = m_vertices[triangle.b];
        Projected c = m_vertices[triangle.c];
        std::int64_t area =
            (static_cast<std::int64_t>(b.x - a.x) * (c.y - a.y)) - (static_cast<std::int64_t>(b.y - a.y) * (c.x - a.x));
        if (area < 0) {
            std::swap(b, c);
            area = -area;
        }
        const int minX = std::max(sx0, std::min({a.x, b.x, c.x}) >> kSubpixelBits);
        const int maxX = std::min(sx1 - 1, std::max({a.x, b.x, c.x}) >> kSubpixelBits);
        const int minY = std::max(sy0, std::min({a.y, b.y, c.y}) >> kSubpixelBits);
        const int maxY = std::min(sy1 - 1, std::max({a.y, b.y, c.y}) >> kSubpixelBits);
        if (minX > maxX || minY > maxY) {
            continue;
        }
        const std::int64_t px = (minX * kSubpixel) + (kSubpixel / 2);
        const std::int64_t py = (minY * kSubpixel) + (kSubpixel / 2);
        const std::int64_t stepX0 = -static_cast<std::int64_t>(c.y - b.y) * kSubpixel;
        const std::int64_t stepY0 = static_cast<std::int64_t>(c.x - b.x) * kSubpixel;
        const std::int64_t stepX1 = -static_cast<std::int64_t>(a.y - c.y) * kSubpixel;
        const std::int64_t stepY1 = static_cast<std::int64_t>(a.x - c.x) * kSubpixel;
        const std::int64_t stepX2 = -static_cast<std::int64_t>(b.y - a.y) * kSubpixel;
        const std::int64_t stepY2 = static_cast<std::int64_t>(b.x - a.x) * kSubpixel;
        std::int64_t row0 =
            (static_cast<std::int64_t>(c.x - b.x) * (py - b.y)) - (static_cast<std::int64_t>(c.y - b.y) * (px - b.x));
        std::int64_t row1 =
            (static_cast<std::int64_t>(a.x - c.x) * (py - c.y)) - (static_cast<std::int64_t>(a.y - c.y) * (px - c.x));
        std::int64_t row2 =
            (static_cast<std::int64_t>(b.x - a.x) * (py - a.y)) - (static_cast<std::int64_t>(b.y - a.y) * (px - a.x));
        const float inverse = 1.0F / static_cast<float>(area);
        const float dz1 = (b.z - a.z) * inverse;
        const float dz2 = (c.z - a.z) * inverse;
        const Normal dn1 = difference(b.normal, a.normal, inverse);
        const Normal dn2 = difference(c.normal, a.normal, inverse);
        const Position dp1 = difference(b.position, a.position, inverse);
        const Position dp2 = difference(c.position, a.position, inverse);
        const std::uint32_t flat = pack(triangle.color[0], triangle.color[1], triangle.color[2]);
        for (int sy = minY; sy <= maxY; ++sy) {
            std::int64_t e0 = row0;
            std::int64_t e1 = row1;
            std::int64_t e2 = row2;
            std::size_t index = (static_cast<std::size_t>(sy - sy0) * static_cast<std::size_t>(size)) +
                                static_cast<std::size_t>(minX - sx0);
            for (int sx = minX; sx <= maxX; ++sx, ++index) {
                if ((e0 | e1 | e2) >= 0) {
                    const auto f1 = static_cast<float>(e1);
                    const auto f2 = static_cast<float>(e2);
                    const float z = a.z + (dz1 * f1) + (dz2 * f2);
                    if (z < depth[index] && z >= -1.0F && z <= 1.0F) {
                        depth[index] = z;
                        if (!triangle.lit) {
                            color[index] = flat;
                        } else {
                            const Normal normal = unit(interpolated(a.normal, dn1, dn2, f1, f2));
                            const Normal view = m_frame.perspective
                                                    ? viewAt(interpolated(a.position, dp1, dp2, f1, f2), m_frame)
                                                    : m_frame.eye;
                            const Linear shaded = lit(normal, view, triangle.linear, m_frame);
                            color[index] =
                                kOpaque | (encoded(shaded[0]) << 16) | (encoded(shaded[1]) << 8) | encoded(shaded[2]);
                        }
                    }
                }
                e0 += stepX0;
                e1 += stepX1;
                e2 += stepX2;
            }
            row0 += stepY0;
            row1 += stepY1;
            row2 += stepY2;
        }
    }

    const int samples = supersample * supersample;
    const auto half = static_cast<std::uint32_t>(samples / 2);
    const auto count = static_cast<std::uint32_t>(samples);
    for (int y = sy0 / supersample; y < sy1 / supersample; ++y) {
        std::uint32_t* line = image.pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width));
        for (int x = sx0 / supersample; x < sx1 / supersample; ++x) {
            std::uint32_t red = 0;
            std::uint32_t green = 0;
            std::uint32_t blue = 0;
            const std::size_t base =
                (static_cast<std::size_t>((y * supersample) - sy0) * static_cast<std::size_t>(size)) +
                static_cast<std::size_t>((x * supersample) - sx0);
            for (int oy = 0; oy < supersample; ++oy) {
                for (int ox = 0; ox < supersample; ++ox) {
                    const std::uint32_t value =
                        color[base + (static_cast<std::size_t>(oy) * static_cast<std::size_t>(size)) +
                              static_cast<std::size_t>(ox)];
                    red += (value >> 16) & 0xFFU;
                    green += (value >> 8) & 0xFFU;
                    blue += value & 0xFFU;
                }
            }
            line[x] =
                kOpaque | (((red + half) / count) << 16) | (((green + half) / count) << 8) | ((blue + half) / count);
        }
    }
}

} // namespace kalve137::render
