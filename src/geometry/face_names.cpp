// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/face_names.h"

#include <BRepTools_History.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>

#include <algorithm>
#include <utility>

namespace kalve137::geometry {

namespace {

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
using AncestorMap = NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher>;

std::string joined(std::vector<std::string> names, char separator)
{
    std::ranges::sort(names);
    std::string result;
    for (const std::string& name : names) {
        if (!result.empty()) {
            result += separator;
        }
        result += name;
    }
    return result;
}

} // namespace

void FaceNames::append(Map& map, const TopoDS_Shape& face, const std::string& name)
{
    if (face.IsNull() || name.empty()) {
        return;
    }
    std::vector<std::string>& names = map[face.TShape()];
    if (std::ranges::find(names, name) == names.end()) {
        names.push_back(name);
    }
}

void FaceNames::add(const TopoDS_Shape& face, const std::string& name)
{
    append(m_names, face, name);
}

const std::vector<std::string>* FaceNames::find(const TopoDS_Shape& face) const
{
    if (face.IsNull()) {
        return nullptr;
    }
    const auto it = m_names.find(face.TShape());
    return it == m_names.end() ? nullptr : &it->second;
}

std::string FaceNames::primary(const TopoDS_Shape& face) const
{
    const std::vector<std::string>* names = find(face);
    return names == nullptr || names->empty() ? std::string() : names->front();
}

void FaceNames::merge(const FaceNames& other)
{
    for (const auto& [face, names] : other.m_names) {
        std::vector<std::string>& target = m_names[face];
        for (const std::string& name : names) {
            if (std::ranges::find(target, name) == target.end()) {
                target.push_back(name);
            }
        }
    }
}

void FaceNames::remap(const std::vector<TopoDS_Shape>& inputs, const BRepTools_History& history,
                      const std::string& prefix)
{
    Map moved;
    const auto carry = [&](const TopoDS_Shape& from, const std::vector<std::string>& names) {
        if (history.IsRemoved(from)) {
            return;
        }
        const NCollection_List<TopoDS_Shape>& modified = history.Modified(from);
        if (modified.IsEmpty()) {
            for (const std::string& name : names) {
                append(moved, from, name);
            }
            return;
        }
        for (const TopoDS_Shape& result : modified) {
            for (const std::string& name : names) {
                append(moved, result, name);
            }
        }
    };
    const auto generate = [&](const TopoDS_Shape& from, const std::string& source) {
        if (source.empty()) {
            return;
        }
        std::string name = prefix;
        name.append("/from(").append(source).append(")");
        for (const TopoDS_Shape& result : history.Generated(from)) {
            if (result.ShapeType() == TopAbs_FACE) {
                append(moved, result, name);
            }
        }
    };
    for (const TopoDS_Shape& input : inputs) {
        if (input.IsNull()) {
            continue;
        }
        ShapeMap faces;
        TopExp::MapShapes(input, TopAbs_FACE, faces);
        for (int i = 1; i <= faces.Extent(); ++i) {
            if (const std::vector<std::string>* names = find(faces(i))) {
                carry(faces(i), *names);
            }
            generate(faces(i), primary(faces(i)));
        }
        AncestorMap edgeFaces;
        TopExp::MapShapesAndUniqueAncestors(input, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
        for (int i = 1; i <= edgeFaces.Extent(); ++i) {
            generate(edgeFaces.FindKey(i), joined(namesAround(edgeFaces(i)), '|'));
        }
        AncestorMap vertexFaces;
        TopExp::MapShapesAndUniqueAncestors(input, TopAbs_VERTEX, TopAbs_FACE, vertexFaces);
        for (int i = 1; i <= vertexFaces.Extent(); ++i) {
            std::vector<std::string> around;
            for (const TopoDS_Shape& face : vertexFaces(i)) {
                around.push_back(primary(face));
            }
            generate(vertexFaces.FindKey(i), joined(std::move(around), '&'));
        }
    }
    m_names = std::move(moved);
}

void FaceNames::prune(const TopoDS_Shape& shape)
{
    Map kept;
    ShapeMap faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i) {
        const auto it = m_names.find(faces(i).TShape());
        if (it != m_names.end()) {
            kept[it->first] = it->second;
        }
    }
    m_names = std::move(kept);
}

void FaceNames::nameRest(const TopoDS_Shape& shape, const std::string& prefix)
{
    ShapeMap faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    int unnamed = 0;
    for (int i = 1; i <= faces.Extent(); ++i) {
        if (find(faces(i)) == nullptr) {
            add(faces(i), prefix + "/new/" + std::to_string(unnamed++));
        }
    }
}

std::vector<std::string> FaceNames::edgeNames(const TopoDS_Shape& shape, const TopoDS_Edge& edge) const
{
    AncestorMap edgeFaces;
    TopExp::MapShapesAndUniqueAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    const NCollection_List<TopoDS_Shape>* faces = edgeFaces.Seek(edge);
    return faces == nullptr ? std::vector<std::string>() : namesAround(*faces);
}

std::vector<std::string> FaceNames::namesAround(const NCollection_List<TopoDS_Shape>& faces) const
{
    if (faces.IsEmpty()) {
        return {};
    }
    std::vector<std::string> names;
    for (const TopoDS_Shape& face : faces) {
        const std::string name = primary(face);
        if (name.empty()) {
            return {};
        }
        names.push_back(name);
    }
    if (names.size() == 1) {
        names.push_back(names.front());
    }
    std::ranges::sort(names);
    return names;
}

} // namespace kalve137::geometry
