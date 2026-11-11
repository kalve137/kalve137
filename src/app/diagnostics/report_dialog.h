// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/diagnostics/diagnostics.h"

#include <filesystem>
#include <optional>

class QWidget;

namespace kalve137::app {

void showProblemReport(QWidget* parent, std::optional<diagnostics::Crash> crash, const std::filesystem::path& design);

}
