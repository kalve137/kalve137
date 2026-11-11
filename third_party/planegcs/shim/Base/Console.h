// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace Base
{
class ConsoleSingleton
{
public:
    template<typename... Args>
    void log(const char*, Args&&...)
    {}

    template<typename... Args>
    void warning(const char*, Args&&...)
    {}
};

inline ConsoleSingleton& Console()
{
    static ConsoleSingleton instance;
    return instance;
}
}
