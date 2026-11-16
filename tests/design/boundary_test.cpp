// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Include {
    std::string file;
    std::string header;
};

std::vector<Include> includesUnder(const fs::path& dir)
{
    static const std::regex kPattern(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
    std::vector<Include> result;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        const std::string extension = entry.path().extension().string();
        if (!entry.is_regular_file() || (extension != ".h" && extension != ".cpp")) {
            continue;
        }
        std::ifstream in(entry.path());
        std::string line;
        std::smatch match;
        while (std::getline(in, line)) {
            if (std::regex_search(line, match, kPattern)) {
                result.push_back({entry.path().lexically_relative(KALVE137_SOURCE_DIR).generic_string(), match[1]});
            }
        }
    }
    return result;
}

bool isKernelHeader(const std::string& header)
{
    return header.ends_with(".hxx") || header.rfind("geometry/", 0) == 0;
}

TEST(BoundaryTest, OnlyTheAdapterIncludesKernelHeaders)
{
    const fs::path root(KALVE137_SOURCE_DIR);
    for (const char* dir :
         {"src/lang", "src/model", "src/rebuild", "src/look", "src/cli", "src/studio", "tests/design"}) {
        for (const Include& include : includesUnder(root / dir)) {
            EXPECT_FALSE(isKernelHeader(include.header)) << include.file << " includes " << include.header;
        }
    }
}

TEST(BoundaryTest, TheAdapterKnowsNoStatements)
{
    for (const Include& include : includesUnder(fs::path(KALVE137_SOURCE_DIR) / "src/kernel")) {
        const bool own = include.header.rfind("kernel/", 0) == 0 || include.header == "model/vec.h";
        const bool project = include.header.find('/') != std::string::npos && !isKernelHeader(include.header);
        EXPECT_TRUE(own || !project) << include.file << " includes " << include.header;
    }
}

TEST(BoundaryTest, OnlyTheSolverAdapterIncludesTheSolver)
{
    const fs::path root(KALVE137_SOURCE_DIR);
    for (const char* dir :
         {"src/lang", "src/model", "src/kernel", "src/rebuild", "src/look", "src/cli", "src/studio", "tests/design"}) {
        for (const Include& include : includesUnder(root / dir)) {
            EXPECT_NE(include.header.rfind("planegcs/", 0), 0U) << include.file << " includes " << include.header;
        }
    }
    for (const Include& include : includesUnder(root / "src/solve")) {
        const bool own = include.header.rfind("solve/", 0) == 0 || include.header.rfind("planegcs/", 0) == 0;
        EXPECT_TRUE(own || include.header.find('/') == std::string::npos)
            << include.file << " includes " << include.header;
    }
}

TEST(BoundaryTest, TheAdapterNamesNoFaces)
{
    static const std::regex kRoleWord(
        R"rx("(start|end|wall|east|west|north|south|top|bottom|mouth|floor|side|cap)")rx");
    for (const auto& entry : fs::recursive_directory_iterator(fs::path(KALVE137_SOURCE_DIR) / "src/kernel")) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::ifstream in(entry.path());
        std::string line;
        int number = 0;
        while (std::getline(in, line)) {
            ++number;
            EXPECT_FALSE(std::regex_search(line, kRoleWord))
                << entry.path().filename().string() << ":" << number << " names a face: " << line;
        }
    }
}

} // namespace
