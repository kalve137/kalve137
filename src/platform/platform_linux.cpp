// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/platform.h"

#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>

namespace kalve137::platform {

namespace {

CrashOptions g_crash;
std::atomic_flag g_crashing = ATOMIC_FLAG_INIT;

[[noreturn]] void handleCrash()
{
    if (!g_crashing.test_and_set() && g_crash.onCrash) {
        std::error_code ec;
        std::filesystem::create_directories(g_crash.folder, ec);
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
        g_crash.onCrash(g_crash.folder / (g_crash.prefix + "-" + stamp));
    }
    std::_Exit(EXIT_FAILURE);
}

void onSignal(int)
{
    handleCrash();
}

} // namespace

std::string_view name()
{
    return "Linux";
}

void writeDebugger(std::string_view) {}

std::uint64_t totalMemory()
{
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && pageSize > 0 ? static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize) : 0;
}

void installCrashHandler(CrashOptions options)
{
    g_crash = std::move(options);
    for (int signal : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
        std::signal(signal, onSignal);
    }
    std::set_terminate([] { handleCrash(); });
}

void crashForTesting()
{
    volatile int* null = nullptr;
    *null = 0x137;
    std::abort();
}

} // namespace kalve137::platform
