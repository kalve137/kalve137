// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/recompute.h"

#include "snapshot/body.h"

#include <TopoDS_Shape.hxx>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace kalve137::core {

class SnapshotCache {
public:
    using Visible = std::function<bool(BodyKey)>;

    std::shared_ptr<const snapshot::Body> body(const Body& body);
    snapshot::Scene scene(const Model& model, const Visible& visible = {});
    void seed(const TopoDS_Shape& shape, std::shared_ptr<const snapshot::Mesh> mesh);
    void clear()
    {
        m_entries.clear();
        m_seeds.clear();
    }
    std::size_t size() const { return m_entries.size(); }

private:
    struct Seed {
        TopoDS_Shape shape;
        std::shared_ptr<const snapshot::Mesh> mesh;
    };

    struct Entry {
        TopoDS_Shape revision;
        std::optional<geometry::Color> color;
        geometry::FaceColors faceColors;
        std::shared_ptr<const snapshot::Body> body;
    };

    std::shared_ptr<const snapshot::Mesh> seedFor(const TopoDS_Shape& shape);

    std::map<BodyKey, Entry> m_entries;
    std::vector<Seed> m_seeds;
};

} // namespace kalve137::core
