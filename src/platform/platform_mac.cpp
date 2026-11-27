// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/platform.h"

#include <sys/resource.h>
#include <sys/sysctl.h>

namespace kalve137::platform {

std::string_view name()
{
    return "macOS";
}

std::uint64_t totalMemory()
{
    std::uint64_t bytes = 0;
    std::size_t size = sizeof(bytes);
    return sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 ? bytes : 0;
}

std::uint64_t peakMemory()
{
    rusage usage{};
    return getrusage(RUSAGE_SELF, &usage) == 0 ? static_cast<std::uint64_t>(usage.ru_maxrss) : 0;
}

} // namespace kalve137::platform
