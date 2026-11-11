// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/platform.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <thread>

namespace {

namespace fs = std::filesystem;

#ifdef _WIN32

constexpr int kCrashExitCode = 0xF137;

fs::path crashFolder()
{
    return fs::temp_directory_path() / "kalve137-crash-test";
}

void crashWithHandler()
{
    kalve137::platform::installCrashHandler({crashFolder(), "kalve137-test", [](const fs::path& base) {
                                                 std::ofstream(fs::path(base).concat(".log")) << "crash log\n";
                                             }});
    std::thread([] { kalve137::platform::crashForTesting(); }).join();
}

TEST(CrashHandler, WritesDumpAndLog)
{
    fs::remove_all(crashFolder());
    EXPECT_EXIT(crashWithHandler(), ::testing::ExitedWithCode(kCrashExitCode), "");

    bool dump = false;
    bool log = false;
    for (const fs::directory_entry& entry : fs::directory_iterator(crashFolder())) {
        dump |= entry.path().extension() == ".dmp" && entry.file_size() > 0;
        log |= entry.path().extension() == ".log";
    }
    EXPECT_TRUE(dump);
    EXPECT_TRUE(log);
    fs::remove_all(crashFolder());
}

#endif

} // namespace
