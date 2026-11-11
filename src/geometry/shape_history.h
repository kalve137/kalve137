// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <BRepTools_History.hxx>
#include <NCollection_List.hxx>
#include <TopoDS_Shape.hxx>

namespace kalve137::geometry {

inline NCollection_List<TopoDS_Shape> only(const TopoDS_Shape& shape)
{
    NCollection_List<TopoDS_Shape> list;
    list.Append(shape);
    return list;
}

inline void record(BRepTools_History* history, const Handle(BRepTools_History)& step)
{
    if (history != nullptr && !step.IsNull()) {
        history->Merge(step);
    }
}

template <typename Algo>
void record(BRepTools_History* history, const TopoDS_Shape& source, Algo& algo)
{
    if (history != nullptr) {
        history->Merge(only(source), algo);
    }
}

} // namespace kalve137::geometry
