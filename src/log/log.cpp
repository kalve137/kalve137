// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "log/log.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/dup_filter_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <mutex>
#include <utility>

namespace kalve137::log {

namespace {

constexpr const char* kPattern = "%Y-%m-%d %H:%M:%S.%e %-5l %-8n [%t] %v";
constexpr const char* kFileName = "kalve137.log";
constexpr std::chrono::seconds kDuplicateWindow{5};

class FunctionSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit FunctionSink(std::function<void(std::string_view)> output) : m_output(std::move(output)) {}

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t buffer;
        formatter_->format(msg, buffer);
        m_output(std::string_view(buffer.data(), buffer.size()));
    }

    void flush_() override {}

private:
    std::function<void(std::string_view)> m_output;
};

struct State {
    State()
    {
        ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(kRingLines);
        filter = std::make_shared<spdlog::sinks::dup_filter_sink_mt>(kDuplicateWindow);
        filter->add_sink(ring);
        for (std::size_t i = 0; i < loggers.size(); ++i) {
            loggers[i] = std::make_shared<spdlog::logger>(std::string(toString(kChannels[i])), filter);
            loggers[i]->set_pattern(kPattern);
            loggers[i]->set_level(Level::info);
        }
    }

    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> ring;
    std::shared_ptr<spdlog::sinks::dup_filter_sink_mt> filter;
    std::array<std::shared_ptr<spdlog::logger>, std::size(kChannels)> loggers;
    std::filesystem::path file;
};

State& state()
{
    static State instance;
    return instance;
}

std::size_t indexOf(Channel channel)
{
    return static_cast<std::size_t>(channel);
}

std::string lower(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<Level> levelFrom(std::string_view name)
{
    const std::string key = lower(name);
    static const std::pair<const char*, Level> kNames[] = {
        {"trace", Level::trace},  {"debug", Level::debug}, {"info", Level::info},         {"warn", Level::warn},
        {"warning", Level::warn}, {"error", Level::err},   {"critical", Level::critical}, {"off", Level::off},
    };
    for (const auto& [text, level] : kNames) {
        if (key == text) {
            return level;
        }
    }
    return std::nullopt;
}

std::string_view levelName(Level level)
{
    switch (level) {
    case Level::trace:
        return "trace";
    case Level::debug:
        return "debug";
    case Level::info:
        return "info";
    case Level::warn:
        return "warn";
    case Level::err:
        return "error";
    case Level::critical:
        return "critical";
    case Level::off:
    case Level::n_levels:
        return "off";
    }
    return "off";
}

} // namespace

std::string_view toString(Channel channel)
{
    switch (channel) {
    case Channel::App:
        return "app";
    case Channel::Core:
        return "core";
    case Channel::Geometry:
        return "geometry";
    case Channel::Sketch:
        return "sketch";
    case Channel::Viewer:
        return "viewer";
    case Channel::Input:
        return "input";
    case Channel::Io:
        return "io";
    }
    return "app";
}

std::optional<Channel> channelFrom(std::string_view name)
{
    const std::string key = lower(name);
    for (Channel channel : kChannels) {
        if (key == toString(channel)) {
            return channel;
        }
    }
    return std::nullopt;
}

std::optional<LevelSpec> parseLevels(std::string_view spec, std::string* error)
{
    LevelSpec result;
    const auto fail = [&](std::string message) -> std::optional<LevelSpec> {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return std::nullopt;
    };
    while (!spec.empty()) {
        const std::size_t comma = spec.find(',');
        const std::string_view item = trim(spec.substr(0, comma));
        spec = comma == std::string_view::npos ? std::string_view() : spec.substr(comma + 1);
        if (item.empty()) {
            continue;
        }
        const std::size_t equals = item.find('=');
        if (equals == std::string_view::npos) {
            const std::optional<Level> level = levelFrom(item);
            if (!level) {
                return fail("Unknown log level " + std::string(item));
            }
            result.all = *level;
            continue;
        }
        const std::string_view name = trim(item.substr(0, equals));
        const std::string_view value = trim(item.substr(equals + 1));
        const std::optional<Channel> channel = channelFrom(name);
        if (!channel) {
            return fail("Unknown log channel " + std::string(name));
        }
        const std::optional<Level> level = levelFrom(value);
        if (!level) {
            return fail("Unknown log level " + std::string(value));
        }
        result.channels[*channel] = *level;
    }
    return result;
}

std::string formatLevels(const LevelSpec& spec)
{
    std::string text;
    if (spec.all) {
        text = levelName(*spec.all);
    }
    for (const auto& [channel, level] : spec.channels) {
        if (!text.empty()) {
            text += ',';
        }
        text += std::string(toString(channel)) + "=" + std::string(levelName(level));
    }
    return text;
}

void init(const Options& options)
{
    State& s = state();
    std::vector<spdlog::sink_ptr> extra;
    s.file.clear();
    if (!options.folder.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(options.folder, ec);
        s.file = options.folder / kFileName;
        try {
            extra.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                s.file.string(), options.maxFileBytes, options.maxFiles, false));
        } catch (const std::exception&) {
            s.file.clear();
        }
    }
    if (options.debuggerOutput) {
        extra.push_back(std::make_shared<FunctionSink>(options.debuggerOutput));
    }
    extra.insert(extra.begin(), s.ring);
    s.filter->set_sinks(std::move(extra));
    for (const auto& logger : s.loggers) {
        logger->set_pattern(kPattern);
    }
    apply(options.levels, options.defaultLevel);
}

void shutdown()
{
    State& s = state();
    for (const auto& logger : s.loggers) {
        logger->flush();
    }
    s.filter->set_sinks({s.ring});
    s.file.clear();
}

void flush()
{
    for (const auto& logger : state().loggers) {
        logger->flush();
    }
}

spdlog::logger& channel(Channel channel)
{
    return *state().loggers[indexOf(channel)];
}

Level level(Channel channel)
{
    return state().loggers[indexOf(channel)]->level();
}

void setLevel(Channel channel, Level level)
{
    spdlog::logger& logger = *state().loggers[indexOf(channel)];
    logger.set_level(level);
    logger.flush_on(std::min(level, Level::info));
}

void apply(const LevelSpec& spec, Level fallback)
{
    const Level all = spec.all.value_or(fallback);
    for (Channel channel : kChannels) {
        auto it = spec.channels.find(channel);
        setLevel(channel, it != spec.channels.end() ? it->second : all);
    }
}

std::vector<std::string> recentLines()
{
    return state().ring->last_formatted();
}

std::filesystem::path currentFile()
{
    return state().file;
}

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

} // namespace kalve137::log
