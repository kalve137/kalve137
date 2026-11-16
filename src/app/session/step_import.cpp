// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/step_import.h"

#include <QString>

#include <format>
#include <string>
#include <utility>

namespace kalve137::app {

core::ImportFeature importFeature(const std::filesystem::path& path, std::vector<geometry::StepPart> parts)
{
    const std::string name = QString::fromStdU16String(path.stem().u16string()).toStdString();
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].name.empty()) {
            parts[i].name = parts.size() == 1 ? name : std::format("{} {}", name, i + 1);
        }
    }
    return {QString::fromStdU16String(path.filename().u16string()).toStdString(), std::move(parts)};
}

} // namespace kalve137::app
