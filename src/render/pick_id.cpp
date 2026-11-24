// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/pick_id.h"

#include <algorithm>
#include <cstdlib>
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

bool isProfile(const PickId& pick)
{
    return pick.chunk == kSketchChunk && pick.kind == PickKind::Face;
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

bool PickKinds::allows(std::uint32_t id) const
{
    const PickId pick = decodePickId(id);
    if (pick.chunk < kBodyChunks) {
        return has(pick.kind);
    }
    if (pick.chunk == kSketchChunk) {
        switch (pick.kind) {
        case PickKind::None:
            return false;
        case PickKind::Face:
            return profiles;
        case PickKind::Edge:
            return sketchLines;
        case PickKind::Point:
            return sketchPoints;
        }
        return false;
    }
    if (pick.chunk == kMarkerChunk) {
        return markers && pick.kind != PickKind::None;
    }
    return pick.kind == PickKind::Edge ? axes : pick.kind != PickKind::None && planes;
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

std::optional<IdHit> resolvePick(const IdImage& image, int x, int y, int radius, const PickKinds& kinds,
                                 int datumRadius, std::span<const std::uint32_t> exactIds)
{
    std::optional<IdHit> best;
    std::tuple<int, int, int, int> bestRank{};
    for (int row = std::max(0, y - radius); row <= std::min(image.height - 1, y + radius); ++row) {
        for (int column = std::max(0, x - radius); column <= std::min(image.width - 1, x + radius); ++column) {
            const std::uint32_t id = image.idAt(column, row);
            if (!kinds.allows(id)) {
                continue;
            }
            const PickId pick = decodePickId(id);
            const int dx = column - x;
            const int dy = row - y;
            if (pick.chunk >= kMarkerChunk && std::max(std::abs(dx), std::abs(dy)) > datumRadius) {
                continue;
            }
            if ((dx != 0 || dy != 0) && std::ranges::find(exactIds, id) != exactIds.end()) {
                continue;
            }
            const std::tuple<int, int, int, int> rank{pick.chunk == kOriginChunk ? 0 : 1, isProfile(pick) ? 0 : 1,
                                                      -priorityOf(pick.kind), (dx * dx) + (dy * dy)};
            if (!best || rank < bestRank) {
                best = IdHit{.id = id, .x = column, .y = row, .depth = image.depthAt(column, row)};
                bestRank = rank;
            }
        }
    }
    return best;
}

} // namespace kalve137::render
