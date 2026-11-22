// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/draw_list.h"
#include "render/thread_pool.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace kalve137::render {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    std::uint32_t at(int x, int y) const
    {
        return pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)];
    }
};

class CpuBackend {
public:
    explicit CpuBackend(int threads = 0);

    void draw(const DrawList& list, int supersample, Image& image);
    int threads() const { return m_pool.size(); }

private:
    struct Projected {
        std::int32_t x = 0;
        std::int32_t y = 0;
        float z = 0.0F;
        Normal normal{};
        Position position{};
        bool valid = false;
    };
    struct Triangle {
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        std::uint32_t c = 0;
        VertexColor color{};
        Linear linear{};
        bool cull = false;
    };
    struct TileBuffers {
        std::vector<std::uint32_t> color;
        std::vector<float> depth;
    };
    struct Bins {
        std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> entries;
        std::vector<std::vector<std::uint32_t>> counts;
        std::vector<std::uint32_t> start;
        std::vector<std::uint32_t> items;
    };

    void project(const DrawList& list);
    void bin();
    void shadeTile(int tile, int worker, Image& image);

    ThreadPool m_pool;
    Frame m_frame;
    bool m_lit = true;
    int m_supersample = 1;
    int m_width = 0;
    int m_height = 0;
    int m_tilesX = 0;
    int m_tilesY = 0;
    int m_tileSamples = 0;
    std::uint32_t m_background = 0;
    std::vector<Projected> m_vertices;
    std::vector<Triangle> m_triangles;
    std::vector<TileBuffers> m_tileBuffers;
    Bins m_bins;
};

} // namespace kalve137::render
