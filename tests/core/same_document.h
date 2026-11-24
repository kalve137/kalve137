// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace kalve137::core::test {

inline void expectSameDocument(const nlohmann::json& shipped, const nlohmann::json& built, const std::string& path)
{
    constexpr double kRelativeTolerance = 1e-12;
    if (shipped.is_number() && built.is_number()) {
        const double a = shipped.get<double>();
        const double b = built.get<double>();
        EXPECT_LE(std::abs(a - b), kRelativeTolerance * std::max(1.0, std::abs(b))) << path;
        return;
    }
    ASSERT_EQ(shipped.type(), built.type()) << path;
    if (shipped.is_object()) {
        ASSERT_EQ(shipped.size(), built.size()) << path;
        for (const auto& [key, value] : built.items()) {
            ASSERT_TRUE(shipped.contains(key)) << path << "/" << key;
            expectSameDocument(shipped.at(key), value, path + "/" += key);
        }
    } else if (shipped.is_array()) {
        ASSERT_EQ(shipped.size(), built.size()) << path;
        for (std::size_t i = 0; i < built.size(); ++i) {
            expectSameDocument(shipped.at(i), built.at(i), path + "/" + std::to_string(i));
        }
    } else {
        EXPECT_EQ(shipped, built) << path;
    }
}

} // namespace kalve137::core::test
