// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/cpu_backend.h"

#include "render/cpu_backend_detail.h"
#include "render/edge_quad.h"
#include "render/style.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>

namespace kalve137::render {

using namespace cpu_backend_detail;

CpuBackend::CpuBackend(int threads) : m_pool(defaultThreads(threads)) {}

void CpuBackend::draw(const DrawList& list, int supersample, Image& image)
{
    m_ids = false;
    prepareTiles(list.frame, supersample);
    image.width = m_width;
    image.height = m_height;
    image.pixels.resize(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height));
    if (m_width == 0 || m_height == 0) {
        return;
    }
    project(list);
    bin();
    m_pool.run(m_tilesX * m_tilesY, [&](int tile, int worker) {
        shadeTile(tile, worker);
        resolveTile(tile, worker, image);
    });
}

void CpuBackend::drawIds(const DrawList& list, const IdStyle& style, IdImage& image)
{
    m_ids = true;
    prepareTiles(list.frame, 1);
    const std::size_t pixels = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    image.width = m_width;
    image.height = m_height;
    image.ids.assign(pixels, 0);
    image.depths.assign(pixels, 1.0F);
    if (pixels == 0) {
        return;
    }
    m_vertices.clear();
    m_triangles.clear();
    const auto chunks = static_cast<std::uint32_t>(std::min<std::size_t>(list.chunks.size(), kBodyChunks));
    for (std::uint32_t i = 0; i < chunks && list.faces; ++i) {
        projectFaces(list, list.chunks[i], encodePickId(i, PickKind::Face, 0));
    }
    for (std::uint32_t i = 0; i < chunks && list.edges; ++i) {
        projectSegments(list, list.chunks[i].segments, list.chunks[i].edgePalette, style.edgeWidth * list.pixelRatio,
                        encodePickId(i, PickKind::Edge, 0));
    }
    for (std::uint32_t i = 0; i < chunks && style.points; ++i) {
        projectSegments(list, list.chunks[i].points, list.chunks[i].pointPalette, style.pointWidth * list.pixelRatio,
                        encodePickId(i, PickKind::Point, 0));
    }
    bin();
    m_pool.run(m_tilesX * m_tilesY, [&](int tile, int worker) {
        shadeTile(tile, worker);
        copyIds(tile, worker, image);
    });
}

void CpuBackend::prepareTiles(const Frame& frame, int supersample)
{
    m_frame = frame;
    m_width = std::max(0, frame.width);
    m_height = std::max(0, frame.height);
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
        buffers.window.resize(m_ids ? tileArea : 0);
        buffers.mask.resize(m_ids ? 0 : tileArea);
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
            if (area == 0 || (m_triangles[i].cull && area > 0)) {
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

void CpuBackend::shadeTile(int tile, int worker)
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
    float* window = buffers.window.data();
    float* mask = buffers.mask.data();
    for (int sy = sy0; sy < sy1; ++sy) {
        const std::size_t row = static_cast<std::size_t>(sy - sy0) * static_cast<std::size_t>(size);
        std::fill_n(color + row, size, m_ids ? 0U : m_background);
        std::fill_n(depth + row, size, std::numeric_limits<float>::infinity());
        if (m_ids) {
            std::fill_n(window + row, size, 1.0F);
        } else {
            std::fill_n(mask + row, size, 0.0F);
        }
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
        const Pass pass = triangle.pass;
        const bool lifted = triangle.hovered || triangle.selected;
        const auto look = [&triangle](const Encoded& base) {
            const Encoded shown = highlighted(base, triangle.hovered, triangle.selected);
            return pack(shown[0] * kByte, shown[1] * kByte, shown[2] * kByte);
        };
        const auto bytes = [](std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
            return Encoded{static_cast<float>(red) / kByte, static_cast<float>(green) / kByte,
                           static_cast<float>(blue) / kByte};
        };
        const std::uint32_t flat = lifted ? look(bytes(triangle.color[0], triangle.color[1], triangle.color[2]))
                                          : pack(triangle.color[0], triangle.color[1], triangle.color[2]);
        const float weight = triangle.weight;
        const auto mixGold = [&triangle, weight](std::uint32_t under) {
            const auto mixed = [&](unsigned shift, std::uint8_t gold) {
                return (static_cast<float>((under >> shift) & 0xFFU) * (1.0F - weight)) +
                       (static_cast<float>(gold) * weight);
            };
            return pack(mixed(16, triangle.color[0]), mixed(8, triangle.color[1]), mixed(0, triangle.color[2]));
        };
        float bias = 0.0F;
        if (triangle.biasSlope != 0.0F || triangle.biasUnits != 0.0F) {
            const float slopeX = std::abs((dz1 * static_cast<float>(stepX1)) + (dz2 * static_cast<float>(stepX2)));
            const float slopeY = std::abs((dz1 * static_cast<float>(stepY1)) + (dz2 * static_cast<float>(stepY2)));
            bias = (triangle.biasSlope * std::max(slopeX, slopeY) * static_cast<float>(supersample)) +
                   (triangle.biasUnits * kDepthUnit);
        }
        const Linear dt1 = difference(b.tint, a.tint, inverse);
        const Linear dt2 = difference(c.tint, a.tint, inverse);
        const auto alpha = static_cast<float>(triangle.color[3]) / kByte;
        const auto blended = [&triangle, alpha](std::uint32_t under) {
            const auto mixed = [&](unsigned shift, std::uint8_t over) {
                return (static_cast<float>((under >> shift) & 0xFFU) * (1.0F - alpha)) +
                       (static_cast<float>(over) * alpha);
            };
            return pack(mixed(16, triangle.color[0]), mixed(8, triangle.color[1]), mixed(0, triangle.color[2]));
        };
        const float dAcross1 = (b.across - a.across) * inverse;
        const float dAcross2 = (c.across - a.across) * inverse;
        const float dAlong1 = (b.along - a.along) * inverse;
        const float dAlong2 = (c.along - a.along) * inverse;
        const auto corePixels = static_cast<float>(m_highlightPixels * 0.5);
        const auto haloPixels = static_cast<float>(m_haloPixels);
        for (int sy = minY; sy <= maxY; ++sy) {
            std::int64_t e0 = row0;
            std::int64_t e1 = row1;
            std::int64_t e2 = row2;
            std::size_t index = (static_cast<std::size_t>(sy - sy0) * static_cast<std::size_t>(size)) +
                                static_cast<std::size_t>(minX - sx0);
            for (int sx = minX; sx <= maxX; ++sx, ++index) {
                if ((e0 | e1 | e2) < 0) {
                    e0 += stepX0;
                    e1 += stepX1;
                    e2 += stepX2;
                    continue;
                }
                const auto f1 = static_cast<float>(e1);
                const auto f2 = static_cast<float>(e2);
                const float z = a.z + (dz1 * f1) + (dz2 * f2);
                const float biased = z + bias;
                const bool clipped = z < -1.0F || z > 1.0F;
                switch (pass) {
                case Pass::Face:
                case Pass::Edge:
                    if ((pass == Pass::Edge ? biased <= depth[index] : biased < depth[index]) && !clipped) {
                        depth[index] = biased;
                        if (m_ids) {
                            color[index] = triangle.id;
                            window[index] = (z * 0.5F) + 0.5F;
                            break;
                        }
                        mask[index] = 0.0F;
                        if (triangle.flat) {
                            color[index] = flat;
                            break;
                        }
                        const Normal normal = unit(interpolated(a.normal, dn1, dn2, f1, f2));
                        const Normal view = m_frame.perspective
                                                ? viewAt(interpolated(a.position, dp1, dp2, f1, f2), m_frame)
                                                : m_frame.eye;
                        const Linear shaded = lit(normal, view, triangle.linear, m_frame);
                        const std::uint32_t red = encoded(shaded[0]);
                        const std::uint32_t green = encoded(shaded[1]);
                        const std::uint32_t blue = encoded(shaded[2]);
                        color[index] =
                            lifted ? look(bytes(static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                                                static_cast<std::uint8_t>(blue)))
                                   : kOpaque | (red << 16) | (green << 8) | blue;
                    }
                    break;
                case Pass::GoldFill:
                    color[index] = mixGold(color[index]);
                    mask[index] = 0.0F;
                    break;
                case Pass::MaskFill:
                    mask[index] = std::max(mask[index], weight);
                    break;
                case Pass::Halo:
                    if (biased <= depth[index] && !clipped) {
                        const float across = a.across + (dAcross1 * f1) + (dAcross2 * f2);
                        const float along = a.along + (dAlong1 * f1) + (dAlong2 * f2);
                        mask[index] =
                            std::max(mask[index], haloStrength(across, along, a.span, corePixels, haloPixels));
                    }
                    break;
                case Pass::GridFill:
                    if (std::clamp(biased, -1.0F, 1.0F) <= depth[index]) {
                        color[index] = blended(color[index]);
                    }
                    break;
                case Pass::GridLine:
                    if (std::clamp(biased, -1.0F, 1.0F) <= depth[index]) {
                        const Linear tint = interpolated(a.tint, dt1, dt2, f1, f2);
                        color[index] = kOpaque | (encoded(tint[0]) << 16) | (encoded(tint[1]) << 8) | encoded(tint[2]);
                    }
                    break;
                case Pass::DatumFill:
                    if (std::clamp(biased, -1.0F, 1.0F) <= depth[index]) {
                        color[index] = blended(color[index]);
                    }
                    break;
                case Pass::DatumLine:
                    if (std::clamp(biased, -1.0F, 1.0F) <= depth[index] &&
                        !stippledOff(triangle, sx / supersample, sy / supersample)) {
                        color[index] = blended(color[index]);
                        depth[index] = std::clamp(biased, -1.0F, 1.0F);
                    }
                    break;
                case Pass::OriginFill:
                    if (z <= depth[index]) {
                        color[index] = blended(color[index]);
                    }
                    break;
                case Pass::OriginLine:
                    if (z <= depth[index] && !stippledOff(triangle, sx / supersample, sy / supersample)) {
                        color[index] = blended(color[index]);
                        depth[index] = z;
                    }
                    break;
                case Pass::DepthReset:
                    depth[index] = std::numeric_limits<float>::infinity();
                    break;
                case Pass::HiddenFace:
                    if (biased > depth[index] && !clipped) {
                        color[index] = mixGold(color[index]);
                        mask[index] = 0.0F;
                    }
                    break;
                case Pass::HiddenEdge:
                    if (z > depth[index] && !clipped &&
                        !dashedOff(triangle.alongX ? sx / supersample : sy / supersample, m_dash, m_gap)) {
                        color[index] = pack(triangle.color[0], triangle.color[1], triangle.color[2]);
                        mask[index] = 0.0F;
                    }
                    break;
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
}

void CpuBackend::resolveTile(int tile, int worker, Image& image) const
{
    const int supersample = m_supersample;
    const int size = m_tileSamples;
    const int sx0 = (tile % m_tilesX) * size;
    const int sy0 = (tile / m_tilesX) * size;
    const int sx1 = std::min(sx0 + size, m_width * supersample);
    const int sy1 = std::min(sy0 + size, m_height * supersample);
    const std::uint32_t* color = m_tileBuffers[static_cast<std::size_t>(worker)].color.data();
    const float* mask = m_tileBuffers[static_cast<std::size_t>(worker)].mask.data();
    const int samples = supersample * supersample;
    const auto half = static_cast<std::uint32_t>(samples / 2);
    const auto count = static_cast<std::uint32_t>(samples);
    for (int y = sy0 / supersample; y < sy1 / supersample; ++y) {
        std::uint32_t* line = image.pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width));
        for (int x = sx0 / supersample; x < sx1 / supersample; ++x) {
            std::uint32_t red = 0;
            std::uint32_t green = 0;
            std::uint32_t blue = 0;
            float push = 0.0F;
            const std::size_t base =
                (static_cast<std::size_t>((y * supersample) - sy0) * static_cast<std::size_t>(size)) +
                static_cast<std::size_t>((x * supersample) - sx0);
            for (int oy = 0; oy < supersample; ++oy) {
                for (int ox = 0; ox < supersample; ++ox) {
                    const std::size_t at = base + (static_cast<std::size_t>(oy) * static_cast<std::size_t>(size)) +
                                           static_cast<std::size_t>(ox);
                    const std::uint32_t value = color[at];
                    red += (value >> 16) & 0xFFU;
                    green += (value >> 8) & 0xFFU;
                    blue += value & 0xFFU;
                    push += mask[at];
                }
            }
            push /= static_cast<float>(count);
            if (push > 0.0F) {
                const auto channel = [count](std::uint32_t sum) {
                    return static_cast<float>(sum) / static_cast<float>(count) / kByte;
                };
                const Encoded shown = pushAway({channel(red), channel(green), channel(blue)}, push);
                line[x] = pack(shown[0] * kByte, shown[1] * kByte, shown[2] * kByte);
                continue;
            }
            line[x] =
                kOpaque | (((red + half) / count) << 16) | (((green + half) / count) << 8) | ((blue + half) / count);
        }
    }
}

void CpuBackend::copyIds(int tile, int worker, IdImage& image) const
{
    const int size = m_tileSamples;
    const int x0 = (tile % m_tilesX) * size;
    const int y0 = (tile / m_tilesX) * size;
    const int x1 = std::min(x0 + size, m_width);
    const int y1 = std::min(y0 + size, m_height);
    const TileBuffers& buffers = m_tileBuffers[static_cast<std::size_t>(worker)];
    for (int y = y0; y < y1; ++y) {
        const std::size_t from = static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(size);
        const std::size_t to = image.indexOf(x0, y);
        std::copy_n(buffers.color.begin() + static_cast<std::ptrdiff_t>(from), x1 - x0,
                    image.ids.begin() + static_cast<std::ptrdiff_t>(to));
        std::copy_n(buffers.window.begin() + static_cast<std::ptrdiff_t>(from), x1 - x0,
                    image.depths.begin() + static_cast<std::ptrdiff_t>(to));
    }
}

} // namespace kalve137::render
