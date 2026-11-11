// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/face_colors.h"

#include <BRepTools_History.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>

namespace kalve137::geometry {

void FaceColors::set(const TopoDS_Face& face, const Color& color)
{
    if (!face.IsNull()) {
        m_colors[face.TShape()] = color;
    }
}

void FaceColors::clear(const TopoDS_Face& face)
{
    if (!face.IsNull()) {
        m_colors.erase(face.TShape());
    }
}

std::optional<Color> FaceColors::find(const TopoDS_Face& face) const
{
    if (face.IsNull()) {
        return std::nullopt;
    }
    const auto it = m_colors.find(face.TShape());
    return it == m_colors.end() ? std::nullopt : std::optional<Color>(it->second);
}

void FaceColors::remap(const TopoDS_Shape& before, const BRepTools_History& history)
{
    std::map<Handle(TopoDS_TShape), Color, ByAddress> moved;
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    TopExp::MapShapes(before, TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i) {
        const TopoDS_Shape& face = faces(i);
        const auto it = m_colors.find(face.TShape());
        if (it == m_colors.end() || history.IsRemoved(face)) {
            continue;
        }
        const NCollection_List<TopoDS_Shape>& modified = history.Modified(face);
        if (modified.IsEmpty()) {
            moved[face.TShape()] = it->second;
            continue;
        }
        for (const TopoDS_Shape& result : modified) {
            moved[result.TShape()] = it->second;
        }
    }
    m_colors = std::move(moved);
}

void FaceColors::prune(const TopoDS_Shape& shape)
{
    std::map<Handle(TopoDS_TShape), Color, ByAddress> kept;
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i) {
        const auto it = m_colors.find(faces(i).TShape());
        if (it != m_colors.end()) {
            kept[it->first] = it->second;
        }
    }
    m_colors = std::move(kept);
}

void FaceColors::merge(const FaceColors& other)
{
    for (const auto& [face, color] : other.m_colors) {
        m_colors[face] = color;
    }
}

std::vector<std::pair<int, Color>> FaceColors::toIndices(const TopoDS_Shape& shape) const
{
    std::vector<std::pair<int, Color>> entries;
    if (m_colors.empty()) {
        return entries;
    }
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i) {
        const auto it = m_colors.find(faces(i).TShape());
        if (it != m_colors.end()) {
            entries.emplace_back(i, it->second);
        }
    }
    return entries;
}

FaceColors FaceColors::fromIndices(const TopoDS_Shape& shape, const std::vector<std::pair<int, Color>>& entries)
{
    FaceColors colors;
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    for (const auto& [index, color] : entries) {
        if (index >= 1 && index <= faces.Extent()) {
            colors.set(TopoDS::Face(faces(index)), color);
        }
    }
    return colors;
}

} // namespace kalve137::geometry
