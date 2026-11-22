// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/pick_id.h"

#include <algorithm>
#include <tuple>

namespace kalve137::render {

namespace {

constexpr std::uint32_t kEntryMask = (1U << kPickKindShift) - 1;
constexpr std::uint32_t kKindMask = 3;

int priorityOf(PickKind kind)
{
    switch (kind) {
    case PickKind::None:
        return 0;
    case PickKind::Face:
        return 1;
    case PickKind::Edge:
        return 2;
    case PickKind::Point:
        return 3;
    }
    return 0;
}

} // namespace

bool PickKinds::has(PickKind kind) const
{
    switch (kind) {
    case PickKind::None:
        return false;
    case PickKind::Face:
        return faces;
    case PickKind::Edge:
        return edges;
    case PickKind::Point:
        return points;
    }
    return false;
}

std::uint32_t encodePickId(std::uint32_t chunk, PickKind kind, std::uint32_t entry)
{
    return (chunk << kPickChunkShift) | (static_cast<std::uint32_t>(kind) << kPickKindShift) | (entry & kEntryMask);
}

PickId decodePickId(std::uint32_t id)
{
    return {.chunk = id >> kPickChunkShift,
            .kind = static_cast<PickKind>((id >> kPickKindShift) & kKindMask),
            .entry = id & kEntryMask};
}

std::optional<IdHit> resolvePick(const IdImage& image, int x, int y, int radius, const PickKinds& kinds)
{
    std::optional<IdHit> best;
    std::tuple<int, int> bestRank{};
    for (int row = std::max(0, y - radius); row <= std::min(image.height - 1, y + radius); ++row) {
        for (int column = std::max(0, x - radius); column <= std::min(image.width - 1, x + radius); ++column) {
            const std::uint32_t id = image.idAt(column, row);
            const PickKind kind = decodePickId(id).kind;
            if (!kinds.has(kind)) {
                continue;
            }
            const int dx = column - x;
            const int dy = row - y;
            const std::tuple<int, int> rank{-priorityOf(kind), (dx * dx) + (dy * dy)};
            if (!best || rank < bestRank) {
                best = IdHit{.id = id, .x = column, .y = row, .depth = image.depthAt(column, row)};
                bestRank = rank;
            }
        }
    }
    return best;
}

} // namespace kalve137::render
