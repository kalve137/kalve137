// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crash_trace.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

#ifdef _WIN32
#include <windows.h>

#include <dbghelp.h>

#include <array>
#endif

namespace kalve137::uitest {

namespace {

#ifdef _WIN32
constexpr DWORD kMaxFrames = 64;
constexpr DWORD kMaxName = 512;

void printStack(CONTEXT* context)
{
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    std::array<void*, kMaxFrames> frames{};
    WORD count = 0;
    if (context == nullptr) {
        count = CaptureStackBackTrace(0, kMaxFrames, frames.data(), nullptr);
    } else {
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context->Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context->Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context->Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        CONTEXT copy = *context;
        while (count < kMaxFrames && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &copy,
                                                 nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            frames[count++] =
                reinterpret_cast<void*>(frame.AddrPC.Offset); // NOLINT(performance-no-int-to-ptr): frame addresses
        }
    }
    std::array<char, sizeof(SYMBOL_INFO) + kMaxName> buffer{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer.data());
    for (WORD i = 0; i < count; ++i) {
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = kMaxName;
        const auto address = reinterpret_cast<DWORD64>(frames[i]);
        DWORD64 offset = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineOffset = 0;
        const bool named = SymFromAddr(process, address, &offset, symbol) != FALSE;
        const bool lined = SymGetLineFromAddr64(process, address, &lineOffset, &line) != FALSE;
        std::fprintf(stderr, "  %2u %s", i, named ? symbol->Name : "?");
        if (lined) {
            std::fprintf(stderr, " %s:%lu", line.FileName, line.LineNumber);
        }
        std::fprintf(stderr, "\n");
    }
    std::fflush(stderr);
}

LONG WINAPI onException(EXCEPTION_POINTERS* info)
{
    std::fprintf(stderr, "\nUnhandled exception 0x%08lx\n", info->ExceptionRecord->ExceptionCode);
    printStack(info->ContextRecord);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void onTerminate()
{
    std::fprintf(stderr, "\nstd::terminate");
    if (const std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& error) {
            std::fprintf(stderr, " with %s", error.what());
        } catch (...) {
            std::fprintf(stderr, " with an unknown exception");
        }
    }
    std::fprintf(stderr, "\n");
#ifdef _WIN32
    printStack(nullptr);
#endif
    std::abort();
}

} // namespace

void installCrashTrace()
{
    std::set_terminate(onTerminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(onException);
    AddVectoredExceptionHandler(0, [](EXCEPTION_POINTERS* info) -> LONG {
        const DWORD code = info->ExceptionRecord->ExceptionCode;
        if (code == STATUS_STACK_BUFFER_OVERRUN || code == EXCEPTION_ACCESS_VIOLATION ||
            code == STATUS_HEAP_CORRUPTION) {
            onException(info);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    });
#endif
}

} // namespace kalve137::uitest
