// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <NCollection_List.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_TShape.hxx>

#include <map>
#include <string>
#include <vector>

class BRepTools_History;

namespace kalve137::geometry {

class FaceNames {
public:
    bool empty() const { return m_names.empty(); }
    std::size_t size() const { return m_names.size(); }

    void add(const TopoDS_Shape& face, const std::string& name);
    const std::vector<std::string>* find(const TopoDS_Shape& face) const;
    std::string primary(const TopoDS_Shape& face) const;
    void merge(const FaceNames& other);

    void remap(const std::vector<TopoDS_Shape>& inputs, const BRepTools_History& history, const std::string& prefix);
    void prune(const TopoDS_Shape& shape);
    void nameRest(const TopoDS_Shape& shape, const std::string& prefix);

    std::vector<std::string> edgeNames(const TopoDS_Shape& shape, const TopoDS_Edge& edge) const;

private:
    struct ByAddress {
        bool operator()(const Handle(TopoDS_TShape)& a, const Handle(TopoDS_TShape)& b) const
        {
            return a.get() < b.get();
        }
    };
    using Map = std::map<Handle(TopoDS_TShape), std::vector<std::string>, ByAddress>;

    static void append(Map& map, const TopoDS_Shape& face, const std::string& name);
    std::vector<std::string> namesAround(const NCollection_List<TopoDS_Shape>& faces) const;

    Map m_names;
};

} // namespace kalve137::geometry
