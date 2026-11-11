// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/diagnostics/problem_report.h"

#include "app/session/paths.h"

#include "log/log.h"

#include <QDateTime>
#include <QStringList>

#include <algorithm>

namespace kalve137::app {

namespace {

constexpr std::size_t kIssueLogLines = 30;

std::string utf8(const QString& text)
{
    return text.toStdString();
}

std::string fileName(const std::filesystem::path& path)
{
    const std::u8string name = path.filename().u8string();
    return {name.begin(), name.end()};
}

QString reportText(const ProblemReport& report)
{
    QStringList lines;
    lines << QStringLiteral("Kalve 137 problem report, %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    lines << QString();
    lines << QStringLiteral("What happened:");
    lines << (report.description.trimmed().isEmpty() ? QStringLiteral("(not described)")
                                                     : report.description.trimmed());
    if (report.crash) {
        lines << QString();
        lines << QStringLiteral("Crash at %1").arg(report.crash->time.toString(Qt::ISODate));
    }
    if (report.includeSystem) {
        lines << QString();
        lines << diagnostics::systemInfo();
    }
    return lines.join('\n') + '\n';
}

} // namespace

std::vector<log::ZipEntry> reportEntries(const ProblemReport& report)
{
    std::vector<log::ZipEntry> entries;
    entries.push_back({"report.txt", {}, utf8(reportText(report))});
    if (report.includeLogs) {
        log::flush();
        std::error_code ec;
        std::vector<std::filesystem::path> logs;
        for (const auto& entry : std::filesystem::directory_iterator(diagnostics::logFolder(), ec)) {
            if (entry.path().extension() == ".log") {
                logs.push_back(entry.path());
            }
        }
        std::ranges::sort(logs);
        for (const std::filesystem::path& path : logs) {
            entries.push_back({"logs/" + fileName(path), path, {}});
        }
    }
    if (report.crash) {
        for (const std::filesystem::path& path : {report.crash->dump, report.crash->log}) {
            if (!path.empty()) {
                entries.push_back({"crash/" + fileName(path), path, {}});
            }
        }
    }
    if (!report.design.empty()) {
        entries.push_back({"design/" + fileName(report.design), report.design, {}});
    }
    return entries;
}

QString issueText(const ProblemReport& report, const std::filesystem::path& zip)
{
    QStringList lines;
    lines << QStringLiteral("### What happened");
    lines << (report.description.trimmed().isEmpty() ? QStringLiteral("_Describe what you did and what went wrong._")
                                                     : report.description.trimmed());
    if (report.crash) {
        lines << QString();
        lines << QStringLiteral("Kalve 137 crashed at %1.").arg(report.crash->time.toString(Qt::ISODate));
    }
    lines << QString();
    lines << QStringLiteral("### System");
    lines << QStringLiteral("```");
    lines << diagnostics::systemInfo();
    lines << QStringLiteral("```");

    const std::vector<std::string> recent = log::recentLines();
    if (!recent.empty()) {
        lines << QString();
        lines << QStringLiteral("### Recent log");
        lines << QStringLiteral("```");
        const std::size_t first = recent.size() > kIssueLogLines ? recent.size() - kIssueLogLines : 0;
        for (std::size_t i = first; i < recent.size(); ++i) {
            lines << QString::fromStdString(recent[i]).trimmed();
        }
        lines << QStringLiteral("```");
    }
    lines << QString();
    lines << (zip.empty() ? QStringLiteral("_Attach the report zip from Help > Report a Problem._")
                          : QStringLiteral("Attached: `%1`").arg(toQString(zip.filename())));
    return lines.join('\n');
}

QString defaultReportName()
{
    return QStringLiteral("kalve137-report-%1.zip").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
}

} // namespace kalve137::app
