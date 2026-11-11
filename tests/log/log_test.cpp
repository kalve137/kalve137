// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "log/log.h"
#include "log/occt_printer.h"
#include "log/report_zip.h"

#include <Message.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace kalve137::log;

bool ringContains(const std::string& text)
{
    const std::vector<std::string> lines = recentLines();
    return std::ranges::any_of(lines, [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

TEST(LogLevels, BareLevelAppliesToEveryChannel)
{
    const auto spec = parseLevels("debug");
    ASSERT_TRUE(spec);
    EXPECT_EQ(spec->all, Level::debug);
    EXPECT_TRUE(spec->channels.empty());
}

TEST(LogLevels, ChannelOverridesAreParsed)
{
    const auto spec = parseLevels(" info , Viewer=trace,input=WARNING ");
    ASSERT_TRUE(spec);
    EXPECT_EQ(spec->all, Level::info);
    ASSERT_EQ(spec->channels.size(), 2u);
    EXPECT_EQ(spec->channels.at(Channel::Viewer), Level::trace);
    EXPECT_EQ(spec->channels.at(Channel::Input), Level::warn);
}

TEST(LogLevels, UnknownNamesAreRejected)
{
    std::string error;
    EXPECT_FALSE(parseLevels("loud", &error));
    EXPECT_NE(error.find("loud"), std::string::npos);
    EXPECT_FALSE(parseLevels("gizmo=debug", &error));
    EXPECT_NE(error.find("gizmo"), std::string::npos);
    EXPECT_FALSE(parseLevels("io=chatty", &error));
}

TEST(LogLevels, FormatRoundTrips)
{
    const auto spec = parseLevels("error,sketch=debug,io=off");
    ASSERT_TRUE(spec);
    EXPECT_EQ(formatLevels(*spec), "error,sketch=debug,io=off");
    EXPECT_EQ(formatLevels({}), "");
}

TEST(LogLevels, ApplySetsEachChannel)
{
    apply(*parseLevels("warn,core=trace"), Level::info);
    EXPECT_EQ(level(Channel::Core), Level::trace);
    EXPECT_EQ(level(Channel::Io), Level::warn);
    apply({}, Level::info);
    EXPECT_EQ(level(Channel::Core), Level::info);
}

TEST(LogRing, KeepsRecentLinesAboveTheLevel)
{
    setLevel(Channel::Core, Level::info);
    channel(Channel::Core).info("ring keeps {}", 137);
    channel(Channel::Core).debug("ring drops {}", 137);
    EXPECT_TRUE(ringContains("ring keeps 137"));
    EXPECT_FALSE(ringContains("ring drops 137"));
    EXPECT_TRUE(ringContains("core"));
}

TEST(LogRing, HoldsAtMostTheRingSize)
{
    for (std::size_t i = 0; i < kRingLines + 10; ++i) {
        channel(Channel::Io).info("filler {}", i);
    }
    const std::vector<std::string> lines = recentLines();
    EXPECT_EQ(lines.size(), kRingLines);
    EXPECT_NE(lines.back().find("filler " + std::to_string(kRingLines + 9)), std::string::npos);
}

TEST(LogOcct, MessagesLandInTheChannel)
{
    routeOcctMessages(Channel::Geometry);
    setLevel(Channel::Geometry, Level::info);
    Message::SendWarning("occt warning reaches the log");
    Message::SendTrace("occt trace stays below the level");
    EXPECT_TRUE(ringContains("OCCT: occt warning reaches the log"));
    EXPECT_FALSE(ringContains("occt trace stays below the level"));
}

class ReportZip : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_folder = std::filesystem::temp_directory_path() / "kalve137-zip-test";
        std::filesystem::remove_all(m_folder);
        std::filesystem::create_directories(m_folder);
    }

    void TearDown() override { std::filesystem::remove_all(m_folder); }

    std::filesystem::path m_folder;
};

TEST_F(ReportZip, HoldsFilesAndMemoryEntries)
{
    const std::filesystem::path log = m_folder / "kalve137.log";
    std::ofstream(log, std::ios::binary) << "a log line\n";
    const std::filesystem::path zip = m_folder / "report.zip";
    writeZip(zip, {{"report.txt", {}, "What happened"}, {"logs/kalve137.log", log, {}}});
    ASSERT_TRUE(std::filesystem::exists(zip));
    EXPECT_EQ(zipEntries(zip), (std::vector<std::string>{"report.txt", "logs/kalve137.log"}));
}

TEST_F(ReportZip, MissingSourceThrowsWithoutWriting)
{
    const std::filesystem::path zip = m_folder / "report.zip";
    EXPECT_THROW(writeZip(zip, {{"crash/missing.dmp", m_folder / "missing.dmp", {}}}), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(zip));
}

} // namespace
