// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/feature.h"
#include "core/parameters.h"

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kalve137::core {

class Document {
public:
    const std::vector<Feature>& features() const { return m_features; }

    const Feature* find(FeatureId id) const;
    Feature* find(FeatureId id);
    int indexOf(FeatureId id) const;

    FeatureId add(FeatureData data, std::string name = {});
    void remove(FeatureId id);

    std::size_t marker() const { return m_marker; }
    void setMarker(std::size_t marker);
    bool isRolledBack(FeatureId id) const;

    std::string takeBodyName();

    void restore(std::vector<Feature> features, std::size_t marker, std::map<std::string, int> counters,
                 FeatureId nextId);
    const std::map<std::string, int>& counters() const { return m_counters; }
    FeatureId nextId() const { return m_nextId; }

    const std::vector<Parameter>& parameters() const { return m_parameters; }
    void setParameters(std::vector<Parameter> parameters) { m_parameters = std::move(parameters); }
    const Parameter* parameter(std::string_view name) const;
    const Feature* featureNamed(std::string_view name) const;

private:
    std::string defaultName(const FeatureData& data);

    std::vector<Feature> m_features;
    std::vector<Parameter> m_parameters;
    std::size_t m_marker = 0;
    std::map<std::string, int> m_counters;
    FeatureId m_nextId = 1;
};

} // namespace kalve137::core
