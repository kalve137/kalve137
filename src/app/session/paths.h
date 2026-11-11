// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <filesystem>

namespace kalve137::app {

inline QString toQString(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

inline std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdU16String());
}

} // namespace kalve137::app
