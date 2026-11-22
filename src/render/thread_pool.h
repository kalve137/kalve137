// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace kalve137::render {

class ThreadPool {
public:
    explicit ThreadPool(int threads);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    int size() const { return static_cast<int>(m_workers.size()) + 1; }
    void run(int count, const std::function<void(int item, int worker)>& job);

private:
    void work(int worker);
    void drain(int worker);

    std::vector<std::thread> m_workers;
    std::mutex m_mutex;
    std::condition_variable m_start;
    std::condition_variable m_done;
    const std::function<void(int, int)>* m_job = nullptr;
    int m_count = 0;
    std::atomic<int> m_next{0};
    int m_busy = 0;
    std::uint64_t m_generation = 0;
    bool m_stop = false;
};

} // namespace kalve137::render
