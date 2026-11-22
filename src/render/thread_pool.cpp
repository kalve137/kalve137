// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/thread_pool.h"

namespace kalve137::render {

ThreadPool::ThreadPool(int threads)
{
    for (int i = 1; i < threads; ++i) {
        m_workers.emplace_back([this, i] { work(i); });
    }
}

ThreadPool::~ThreadPool()
{
    {
        const std::scoped_lock lock(m_mutex);
        m_stop = true;
    }
    m_start.notify_all();
    for (std::thread& worker : m_workers) {
        worker.join();
    }
}

void ThreadPool::run(int count, const std::function<void(int, int)>& job)
{
    if (m_workers.empty()) {
        for (int i = 0; i < count; ++i) {
            job(i, 0);
        }
        return;
    }
    {
        const std::scoped_lock lock(m_mutex);
        m_job = &job;
        m_count = count;
        m_next = 0;
        m_busy = static_cast<int>(m_workers.size());
        ++m_generation;
    }
    m_start.notify_all();
    drain(0);
    std::unique_lock lock(m_mutex);
    m_done.wait(lock, [this] { return m_busy == 0; });
}

void ThreadPool::work(int worker)
{
    std::uint64_t seen = 0;
    std::unique_lock lock(m_mutex);
    while (true) {
        m_start.wait(lock, [&] { return m_stop || m_generation != seen; });
        if (m_stop) {
            return;
        }
        seen = m_generation;
        lock.unlock();
        drain(worker);
        lock.lock();
        if (--m_busy == 0) {
            m_done.notify_one();
        }
    }
}

void ThreadPool::drain(int worker)
{
    while (true) {
        const int item = m_next.fetch_add(1);
        if (item >= m_count) {
            return;
        }
        (*m_job)(item, worker);
    }
}

} // namespace kalve137::render
