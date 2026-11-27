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

TEST(Version, MacroMatchesConfigure)
{
    EXPECT_EQ(KALVE137_VERSION,
              composeVersion(KALVE137_TEST_PROJECT_VERSION, KALVE137_TEST_VERSION_BASE, KALVE137_TEST_VERSION_SUFFIX));
}

TEST(Version, NightlyLabel)
{
    EXPECT_EQ(composeVersion("0.1.0", "0.2.0", "-dev.20251124+abc123"), "0.2.0-dev.20251124+abc123");
    EXPECT_EQ(composeVersion("0.1.0", "", ""), "0.1.0");
    EXPECT_EQ(composeVersion("0.1.0", "", "-rc.1"), "0.1.0-rc.1");
}
