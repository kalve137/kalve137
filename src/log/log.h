// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <spdlog/spdlog.h>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kalve137::log {

enum class Channel { App, Core, Geometry, Sketch, Viewer, Input, Io };

inline constexpr Channel kChannels[] = {Channel::App,    Channel::Core,  Channel::Geometry, Channel::Sketch,
                                        Channel::Viewer, Channel::Input, Channel::Io};

using Level = spdlog::level::level_enum;

std::string_view toString(Channel channel);
std::optional<Channel> channelFrom(std::string_view name);

struct LevelSpec {
    std::optional<Level> all;
    std::map<Channel, Level> channels;
};
std::optional<LevelSpec> parseLevels(std::string_view spec, std::string* error = nullptr);
std::string formatLevels(const LevelSpec& spec);

struct Options {
    std::filesystem::path folder;
    std::size_t maxFileBytes = std::size_t{5} * 1024 * 1024;
    std::size_t maxFiles = 5;
    Level defaultLevel = Level::info;
    LevelSpec levels;
    std::function<void(std::string_view)> debuggerOutput;
};

void init(const Options& options);
void shutdown();
void flush();

spdlog::logger& channel(Channel channel);
Level level(Channel channel);
void setLevel(Channel channel, Level level);
void apply(const LevelSpec& spec, Level fallback);

inline constexpr std::size_t kRingLines = 2000;
std::vector<std::string> recentLines();
std::filesystem::path currentFile();

std::string utf8(const std::filesystem::path& path);

} // namespace kalve137::log

#define K137_TRACE(ch, ...) SPDLOG_LOGGER_TRACE(&::kalve137::log::channel(::kalve137::log::Channel::ch), __VA_ARGS__)
#define K137_DEBUG(ch, ...) SPDLOG_LOGGER_DEBUG(&::kalve137::log::channel(::kalve137::log::Channel::ch), __VA_ARGS__)
#define K137_INFO(ch, ...) SPDLOG_LOGGER_INFO(&::kalve137::log::channel(::kalve137::log::Channel::ch), __VA_ARGS__)
#define K137_WARN(ch, ...) SPDLOG_LOGGER_WARN(&::kalve137::log::channel(::kalve137::log::Channel::ch), __VA_ARGS__)
#define K137_ERROR(ch, ...) SPDLOG_LOGGER_ERROR(&::kalve137::log::channel(::kalve137::log::Channel::ch), __VA_ARGS__)
