// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/diagnostics/diagnostics.h"

#include "log/report_zip.h"

#include <QString>

#include <filesystem>
#include <optional>
#include <vector>

namespace kalve137::app {

struct ProblemReport {
    QString description;
    bool includeLogs = true;
    bool includeSystem = true;
    std::optional<diagnostics::Crash> crash;
    std::filesystem::path design;
};

std::vector<log::ZipEntry> reportEntries(const ProblemReport& report);
QString issueText(const ProblemReport& report, const std::filesystem::path& zip = {});
QString defaultReportName();

} // namespace kalve137::app
