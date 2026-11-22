// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/draw_list.h"
#include "render/pick_id.h"
#include "render/thread_pool.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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
    void drawIds(const DrawList& list, const IdStyle& style, IdImage& image);
    int threads() const { return m_pool.size(); }

private:
    struct Projected {
        std::int32_t x = 0;
        std::int32_t y = 0;
        float z = 0.0F;
        Normal normal{};
        Position position{};
        Linear tint{};
        float across = 0.0F;
        float along = 0.0F;
        float span = 0.0F;
        bool valid = false;
    };
    enum class Pass : std::uint8_t { Face, GoldFill, MaskFill, Halo, Edge, GridFill, GridLine, HiddenFace, HiddenEdge };
    struct Triangle {
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        std::uint32_t c = 0;
        VertexColor color{};
        Linear linear{};
        Pass pass = Pass::Face;
        bool hovered = false;
        bool selected = false;
        float weight = 0.0F;
        bool cull = false;
        bool flat = false;
        float biasSlope = 0.0F;
        float biasUnits = 0.0F;
        bool alongX = false;
        std::uint32_t id = 0;
    };
    struct TileBuffers {
        std::vector<std::uint32_t> color;
        std::vector<float> depth;
        std::vector<float> window;
        std::vector<float> mask;
    };
    struct Bins {
        std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> entries;
        std::vector<std::vector<std::uint32_t>> counts;
        std::vector<std::uint32_t> start;
        std::vector<std::uint32_t> items;
    };

    void prepareTiles(const Frame& frame, int supersample);
    void project(const DrawList& list);
    void projectFaces(const DrawList& list, const Chunk& item, std::uint32_t id);
    void projectSegments(const DrawList& list, std::span<const Segment> segments, std::span<const Entry> palette,
                         double widthPixels, std::uint32_t id);
    void projectHighlight(const DrawList& list, std::span<const Position> faces, Pass pass, float weight, float bias);
    void projectLines(const DrawList& list, std::span<const Segment> segments, double widthPixels, Pass pass);
    bool addQuad(const DrawList& list, const Segment& segment, double widthPixels, const Triangle& quad);
    void projectGrid(const DrawList& list);
    void addGridLine(const DrawList& list, const OverlayLine& line,
                     const std::optional<std::array<double, 3>>& onPlane);
    void bin();
    void shadeTile(int tile, int worker);
    void resolveTile(int tile, int worker, Image& image) const;
    void copyIds(int tile, int worker, IdImage& image) const;

    ThreadPool m_pool;
    Frame m_frame;
    int m_supersample = 1;
    int m_width = 0;
    int m_height = 0;
    int m_tilesX = 0;
    int m_tilesY = 0;
    int m_tileSamples = 0;
    std::uint32_t m_background = 0;
    VertexColor m_edgeColor{};
    double m_edgePixels = 0.0;
    double m_highlightPixels = 0.0;
    double m_haloPixels = 0.0;
    float m_dash = 0.0F;
    float m_gap = 0.0F;
    bool m_highlightedEdgesOnly = false;
    bool m_ids = false;
    std::vector<Projected> m_vertices;
    std::vector<Triangle> m_triangles;
    std::vector<TileBuffers> m_tileBuffers;
    Bins m_bins;
};

} // namespace kalve137::render
