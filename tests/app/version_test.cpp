// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace {

std::string composeVersion(const char* project, const char* base, const char* suffix)
{
    const bool baseSet = base != nullptr && base[0] != '\0';
    const bool suffixSet = suffix != nullptr && suffix[0] != '\0';
    if (baseSet) {
        return std::string(base) + (suffixSet ? suffix : "");
    }
    if (suffixSet) {
        return std::string(project) + suffix;
    }
    return project;
}

} // namespace

TEST(Version, MacroIsNonEmpty)
{
    EXPECT_GT(std::strlen(KALVE137_VERSION), 0u);
}

TEST(Version, DefaultIsProjectVersion)
{
    EXPECT_STREQ(KALVE137_VERSION, "0.1.0");
}

TEST(Version, NightlyLabel)
{
    EXPECT_EQ(composeVersion("0.1.0", "0.2.0", "-dev.20251124+abc123"), "0.2.0-dev.20251124+abc123");
    EXPECT_EQ(composeVersion("0.1.0", "", ""), "0.1.0");
    EXPECT_EQ(composeVersion("0.1.0", "", "-rc.1"), "0.1.0-rc.1");
}
