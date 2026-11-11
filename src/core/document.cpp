// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace kalve137::core {

const Feature* Document::find(FeatureId id) const
{
    auto it = std::ranges::find(m_features, id, &Feature::id);
    return it == m_features.end() ? nullptr : &*it;
}

Feature* Document::find(FeatureId id)
{
    auto it = std::ranges::find(m_features, id, &Feature::id);
    return it == m_features.end() ? nullptr : &*it;
}

int Document::indexOf(FeatureId id) const
{
    auto it = std::ranges::find(m_features, id, &Feature::id);
    return it == m_features.end() ? -1 : static_cast<int>(it - m_features.begin());
}

FeatureId Document::add(FeatureData data, std::string name)
{
    Feature feature;
    feature.id = m_nextId++;
    feature.name = name.empty() ? defaultName(data) : std::move(name);
    feature.data = std::move(data);
    const FeatureId id = feature.id;
    m_features.insert(m_features.begin() + static_cast<std::ptrdiff_t>(m_marker), std::move(feature));
    ++m_marker;
    return id;
}

void Document::remove(FeatureId id)
{
    const int index = indexOf(id);
    if (index < 0) {
        return;
    }
    m_features.erase(m_features.begin() + index);
    if (static_cast<std::size_t>(index) < m_marker) {
        --m_marker;
    }
}

void Document::setMarker(std::size_t marker)
{
    m_marker = std::min(marker, m_features.size());
}

bool Document::isRolledBack(FeatureId id) const
{
    const int index = indexOf(id);
    return index >= 0 && static_cast<std::size_t>(index) >= m_marker;
}

std::string Document::takeBodyName()
{
    return "Body" + std::to_string(++m_counters["body"]);
}

void Document::restore(std::vector<Feature> features, std::size_t marker, std::map<std::string, int> counters,
                       FeatureId nextId)
{
    m_features = std::move(features);
    m_counters = std::move(counters);
    m_nextId = nextId;
    for (const Feature& feature : m_features) {
        m_nextId = std::max(m_nextId, feature.id + 1);
    }
    setMarker(marker);
}

std::string Document::defaultName(const FeatureData& data)
{
    const std::string type(typeName(data));
    const int number = ++m_counters[type];
    std::string prefix;
    bool upper = true;
    for (const char c : type) {
        if (c == '_') {
            upper = true;
            continue;
        }
        prefix += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        upper = false;
    }
    return prefix + std::to_string(number);
}

} // namespace kalve137::core
