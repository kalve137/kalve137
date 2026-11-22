// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/platform.h"

#include <sys/resource.h>
#include <unistd.h>

namespace kalve137::platform {

std::string_view name()
{
    return "Linux";
}

std::uint64_t totalMemory()
{
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && pageSize > 0 ? static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize) : 0;
}

std::uint64_t peakMemory()
{
    constexpr std::uint64_t kBytesPerKilobyte = 1024;
    rusage usage{};
    return getrusage(RUSAGE_SELF, &usage) == 0 ? static_cast<std::uint64_t>(usage.ru_maxrss) * kBytesPerKilobyte : 0;
}

} // namespace kalve137::platform
