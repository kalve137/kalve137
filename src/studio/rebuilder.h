// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "model/snapshot.h"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace kalve137::studio {

class Rebuilder {
public:
    using Done = std::function<void(int generation, std::shared_ptr<const model::Snapshot>)>;

    explicit Rebuilder(Done done);
    ~Rebuilder();

    Rebuilder(const Rebuilder&) = delete;
    Rebuilder& operator=(const Rebuilder&) = delete;

    int request(std::string source);

private:
    void loop();

    Done m_done;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::optional<std::pair<int, std::string>> m_pending;
    int m_generation = 0;
    bool m_quit = false;
    std::thread m_thread;
};

} // namespace kalve137::studio
