// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/diagnostics/diagnostics.h"

#include "app/session/paths.h"

#include "log/log.h"
#include "log/occt_printer.h"
#include "platform/platform.h"

#include <Standard_Version.hxx>

#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QStringList>
#include <QSysInfo>
#include <QThread>

#include <fstream>
#include <string>

namespace kalve137::app::diagnostics {

namespace {

constexpr const char* kEnvLevels = "KALVE137_LOG";
constexpr const char* kPendingMarker = "pending.txt";
constexpr const char* kCategoryPrefix = "kalve137.";

QString g_glInfo;

log::Level defaultLevel()
{
#ifdef NDEBUG
    return log::Level::info;
#else
    return log::Level::debug;
#endif
}

std::optional<log::LevelSpec> envLevels()
{
    const QByteArray value = qgetenv(kEnvLevels);
    if (value.isEmpty()) {
        return std::nullopt;
    }
    std::string error;
    std::optional<log::LevelSpec> spec = log::parseLevels(value.toStdString(), &error);
    if (!spec) {
        K137_WARN(App, "Ignoring {}: {}", kEnvLevels, error);
    }
    return spec;
}

log::Level levelOf(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return log::Level::debug;
    case QtInfoMsg:
        return log::Level::info;
    case QtWarningMsg:
        return log::Level::warn;
    case QtCriticalMsg:
        return log::Level::err;
    case QtFatalMsg:
        return log::Level::critical;
    }
    return log::Level::info;
}

void qtMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const std::string_view category = context.category != nullptr ? context.category : "default";
    log::Channel channel = log::Channel::App;
    std::string prefix;
    if (category.starts_with(kCategoryPrefix)) {
        channel = log::channelFrom(category.substr(std::char_traits<char>::length(kCategoryPrefix)))
                      .value_or(log::Channel::App);
    } else if (category != "default") {
        prefix = "[" + std::string(category) + "] ";
    }
    log::channel(channel).log(levelOf(type), "{}{}", prefix, message.toStdString());
    if (type == QtFatalMsg) {
        log::flush();
    }
}

std::filesystem::path appDataFolder()
{
    return toPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
}

void writeLines(const std::filesystem::path& path, const std::vector<std::string>& lines)
{
    std::ofstream file(path, std::ios::binary);
    for (const std::string& line : lines) {
        file << line;
    }
}

std::optional<Crash> crashAt(const std::filesystem::path& base)
{
    Crash crash;
    std::error_code ec;
    const std::filesystem::path dump = std::filesystem::path(base).concat(".dmp");
    const std::filesystem::path log = std::filesystem::path(base).concat(".log");
    if (std::filesystem::exists(dump, ec)) {
        crash.dump = dump;
    }
    if (std::filesystem::exists(log, ec)) {
        crash.log = log;
    }
    if (crash.dump.empty() && crash.log.empty()) {
        return std::nullopt;
    }
    const std::filesystem::path& any = crash.dump.empty() ? crash.log : crash.dump;
    const auto written = std::filesystem::last_write_time(any, ec);
    if (!ec) {
        const auto system = std::chrono::clock_cast<std::chrono::system_clock>(written);
        crash.time = QDateTime::fromMSecsSinceEpoch(
            std::chrono::duration_cast<std::chrono::milliseconds>(system.time_since_epoch()).count());
    }
    return crash;
}

QString megabytes(std::uint64_t bytes)
{
    return QStringLiteral("%1 MB").arg(bytes / (std::uint64_t{1024} * 1024));
}

} // namespace

std::filesystem::path logFolder()
{
    return appDataFolder() / "logs";
}

std::filesystem::path crashFolder()
{
    return appDataFolder() / "crashes";
}

void startLogging()
{
    log::Options options;
    options.folder = logFolder();
    options.defaultLevel = defaultLevel();
    options.levels = envLevels().value_or(log::LevelSpec{});
    options.debuggerOutput = platform::writeDebugger;
    log::init(options);
    qInstallMessageHandler(qtMessage);
    log::routeOcctMessages(log::Channel::Viewer);

    K137_INFO(App, "Kalve 137 {} started", KALVE137_VERSION);
    for (const QString& line : systemInfo().split('\n')) {
        K137_INFO(App, "  {}", line.toStdString());
    }
}

void stopLogging()
{
    K137_INFO(App, "Kalve 137 closed");
    qInstallMessageHandler(nullptr);
    log::shutdown();
}

void applyLevels(const QString& preferenceSpec)
{
    if (std::optional<log::LevelSpec> env = envLevels()) {
        log::apply(*env, defaultLevel());
        return;
    }
    std::string error;
    std::optional<log::LevelSpec> spec = log::parseLevels(preferenceSpec.toStdString(), &error);
    if (!spec) {
        K137_WARN(App, "Ignoring the log level preference: {}", error);
        spec = log::LevelSpec{};
    }
    log::apply(*spec, defaultLevel());
}

void installCrashHandler()
{
    const std::filesystem::path folder = crashFolder();
    platform::CrashOptions options;
    options.folder = folder;
    options.prefix = std::string("kalve137-") + KALVE137_VERSION;
    options.onCrash = [folder](const std::filesystem::path& base) {
        K137_ERROR(App, "Crashed, dump at {}", log::utf8(base));
        log::flush();
        writeLines(std::filesystem::path(base).concat(".log"), log::recentLines());
        std::ofstream marker(folder / kPendingMarker, std::ios::binary);
        const std::u8string text = base.u8string();
        marker.write(reinterpret_cast<const char*>(text.data()), static_cast<std::streamsize>(text.size()));
    };
    platform::installCrashHandler(std::move(options));
}

std::optional<Crash> takePendingCrash()
{
    const std::filesystem::path markerPath = crashFolder() / kPendingMarker;
    std::ifstream marker(markerPath, std::ios::binary);
    if (!marker) {
        return std::nullopt;
    }
    std::string base((std::istreambuf_iterator<char>(marker)), std::istreambuf_iterator<char>());
    marker.close();
    std::error_code ec;
    std::filesystem::remove(markerPath, ec);
    const std::u8string utf8(base.begin(), base.end());
    std::optional<Crash> crash = crashAt(std::filesystem::path(utf8));
    if (crash) {
        K137_WARN(App, "The previous run crashed, report at {}", base);
    }
    return crash;
}

std::optional<Crash> latestCrash()
{
    std::error_code ec;
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime;
    for (const auto& entry : std::filesystem::directory_iterator(crashFolder(), ec)) {
        const std::filesystem::path& path = entry.path();
        if (path.extension() != ".dmp" && path.extension() != ".log") {
            continue;
        }
        const auto time = entry.last_write_time(ec);
        if (!ec && (newest.empty() || time > newestTime)) {
            newest = path;
            newestTime = time;
        }
    }
    if (newest.empty()) {
        return std::nullopt;
    }
    return crashAt(newest.replace_extension());
}

void setGlInfo(const QString& info)
{
    g_glInfo = info;
    for (const QString& line : info.split('\n')) {
        K137_INFO(Viewer, "  {}", line.toStdString());
    }
}

QString systemInfo()
{
    QStringList lines;
    lines << QStringLiteral("Version: %1 (%2, %3)").arg(KALVE137_VERSION, KALVE137_GIT_HASH, KALVE137_BUILD_TYPE);
    lines << QStringLiteral("OS: %1 (%2 %3, %4)")
                 .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion(),
                      QSysInfo::currentCpuArchitecture());
    lines << QStringLiteral("CPU threads: %1, memory: %2")
                 .arg(QThread::idealThreadCount())
                 .arg(megabytes(platform::totalMemory()));
    lines << QStringLiteral("Qt: %1, OpenCascade: %2").arg(QString::fromLatin1(qVersion()), OCC_VERSION_COMPLETE);
    if (const auto* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        for (const QScreen* screen : gui->screens()) {
            lines << QStringLiteral("Screen: %1 %2x%3 at %4x scale")
                         .arg(screen->name())
                         .arg(screen->size().width())
                         .arg(screen->size().height())
                         .arg(screen->devicePixelRatio());
        }
    }
    if (!g_glInfo.isEmpty()) {
        lines << g_glInfo.split('\n');
    }
    return lines.join('\n');
}

} // namespace kalve137::app::diagnostics
