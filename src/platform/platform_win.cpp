// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/platform.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dbghelp.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

// NOLINTBEGIN(readability-identifier-naming): The drivers look these symbols up by name in the export table.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
// NOLINTEND(readability-identifier-naming)

namespace kalve137::platform {

namespace {

constexpr ULONG kStackGuaranteeBytes = 64 * 1024;
constexpr UINT kCrashExitCode = 0xF137;

CrashOptions g_crash;
std::atomic_flag g_crashing = ATOMIC_FLAG_INIT;

std::wstring widen(std::string_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::filesystem::path crashBase()
{
    SYSTEMTIME time;
    GetLocalTime(&time);
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04u%02u%02u-%02u%02u%02u", time.wYear, time.wMonth, time.wDay, time.wHour,
                  time.wMinute, time.wSecond);
    return g_crash.folder / (g_crash.prefix + "-" + stamp);
}

void writeDump(const std::filesystem::path& path, EXCEPTION_POINTERS* exception)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                 MiniDumpWithUnloadedModules);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, exception != nullptr ? &info : nullptr,
                      nullptr, nullptr);
    CloseHandle(file);
}

[[noreturn]] void handleCrash(EXCEPTION_POINTERS* exception)
{
    if (!g_crashing.test_and_set()) {
        std::error_code ec;
        std::filesystem::create_directories(g_crash.folder, ec);
        const std::filesystem::path base = crashBase();
        writeDump(std::filesystem::path(base).concat(".dmp"), exception);
        if (g_crash.onCrash) {
            g_crash.onCrash(base);
        }
    }
    TerminateProcess(GetCurrentProcess(), kCrashExitCode);
    std::abort();
}

LONG WINAPI unhandledException(EXCEPTION_POINTERS* exception)
{
    handleCrash(exception);
}

[[noreturn]] void raiseCrash()
{
    __try {
        RaiseException(0xE0F13700, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    } __except (handleCrash(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER) {
    }
    std::abort();
}

void onTerminate()
{
    raiseCrash();
}

void onPureCall()
{
    raiseCrash();
}

void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    raiseCrash();
}

void onAbort(int)
{
    raiseCrash();
}

} // namespace

std::string_view name()
{
    return "Windows";
}

void writeDebugger(std::string_view text)
{
    if (IsDebuggerPresent()) {
        OutputDebugStringW(widen(text).c_str());
    }
}

std::uint64_t totalMemory()
{
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
}

void installCrashHandler(CrashOptions options)
{
    g_crash = std::move(options);
    ULONG guarantee = kStackGuaranteeBytes;
    SetThreadStackGuarantee(&guarantee);
    SetUnhandledExceptionFilter(unhandledException);
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParameter);
    std::signal(SIGABRT, onAbort);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

void crashForTesting()
{
    volatile int* null = nullptr;
    *null = 0x137; // NOLINT(clang-analyzer-core.NullDereference): The crash is the point.
    std::abort();
}

} // namespace kalve137::platform
