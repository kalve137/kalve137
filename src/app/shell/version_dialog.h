// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>

class QWidget;

namespace kalve137::app {

std::optional<std::filesystem::path> chooseVersion(QWidget* parent, const std::filesystem::path& design);

}
