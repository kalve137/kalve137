// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/history.h"
#include "core/project.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace kalve137::core;

class History : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_folder = std::filesystem::temp_directory_path() / "kalve137-history-test";
        std::filesystem::remove_all(m_folder);
        std::filesystem::create_directories(m_folder);
        m_design = m_folder / "part.k137";
    }

    void TearDown() override { std::filesystem::remove_all(m_folder); }

    void write(const std::string& text) const { std::ofstream(m_design, std::ios::binary) << text; }

    static std::string read(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    std::filesystem::path m_folder;
    std::filesystem::path m_design;
};

TEST_F(History, NothingToArchiveBeforeFirstSave)
{
    archiveVersion(m_design);
    EXPECT_TRUE(versions(m_design).empty());
}

TEST_F(History, KeepsEachEarlierSaveNewestFirst)
{
    write("first");
    archiveVersion(m_design);
    write("second");
    archiveVersion(m_design);
    write("third");

    const std::vector<Version> found = versions(m_design);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0].number, 2);
    EXPECT_EQ(read(found[0].path), "second");
    EXPECT_EQ(found[1].number, 1);
    EXPECT_EQ(read(found[1].path), "first");
}

TEST_F(History, PrunesOldestButNeverReusesNumbers)
{
    for (int i = 1; i <= 5; ++i) {
        write("save " + std::to_string(i));
        archiveVersion(m_design, 3);
    }
    const std::vector<Version> found = versions(m_design);
    ASSERT_EQ(found.size(), 3u);
    EXPECT_EQ(found[0].number, 5);
    EXPECT_EQ(found[2].number, 3);
    EXPECT_EQ(read(found[2].path), "save 3");
}

TEST_F(History, ProjectsDoNotListHistoryAsDesigns)
{
    const Project project = Project::create(m_folder, "History");
    write("first");
    archiveVersion(m_design);
    EXPECT_TRUE(std::filesystem::is_regular_file(m_folder / kHistoryFolderName / "part.k137" / "v1.k137"));
    ASSERT_EQ(project.designs().size(), 1u);
    EXPECT_EQ(project.designs().front().filename(), "part.k137");
}

} // namespace
