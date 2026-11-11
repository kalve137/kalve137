// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_TShape.hxx>

#include <map>
#include <optional>
#include <utility>
#include <vector>

class BRepTools_History;

namespace kalve137::geometry {

struct Color {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;

    bool operator==(const Color&) const = default;
};

class FaceColors {
public:
    bool empty() const { return m_colors.empty(); }
    std::size_t size() const { return m_colors.size(); }

    void set(const TopoDS_Face& face, const Color& color);
    void clear(const TopoDS_Face& face);
    void clear() { m_colors.clear(); }
    std::optional<Color> find(const TopoDS_Face& face) const;

    void remap(const TopoDS_Shape& before, const BRepTools_History& history);
    void prune(const TopoDS_Shape& shape);
    void merge(const FaceColors& other);

    std::vector<std::pair<int, Color>> toIndices(const TopoDS_Shape& shape) const;
    static FaceColors fromIndices(const TopoDS_Shape& shape, const std::vector<std::pair<int, Color>>& entries);

    bool operator==(const FaceColors&) const = default;

private:
    struct ByAddress {
        bool operator()(const Handle(TopoDS_TShape)& a, const Handle(TopoDS_TShape)& b) const
        {
            return a.get() < b.get();
        }
    };

    std::map<Handle(TopoDS_TShape), Color, ByAddress> m_colors;
};

} // namespace kalve137::geometry
