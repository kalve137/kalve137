// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "studio/rebuilder.h"

#include "rebuild/rebuild.h"

#include <exception>
#include <utility>

namespace kalve137::studio {

Rebuilder::Rebuilder(Done done) : m_done(std::move(done)), m_thread([this] { loop(); }) {}

Rebuilder::~Rebuilder()
{
    {
        const std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_one();
    m_thread.join();
}

int Rebuilder::request(std::string source)
{
    int generation = 0;
    {
        const std::lock_guard lock(m_mutex);
        generation = ++m_generation;
        m_pending = {generation, std::move(source)};
    }
    m_wake.notify_one();
    return generation;
}

void Rebuilder::loop()
{
    while (true) {
        std::pair<int, std::string> job;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_quit || m_pending.has_value(); });
            if (m_quit) {
                return;
            }
            job = std::move(*m_pending);
            m_pending.reset();
        }
        model::Snapshot built;
        try {
            built = rebuild::build(job.second);
        } catch (const std::exception& error) {
            built.diagnostics.push_back({lang::Diagnostic::Severity::Error,
                                         "internal",
                                         {},
                                         {},
                                         std::string("The rebuild stopped on an internal error: ") + error.what(),
                                         {}});
        }
        m_done(job.first, std::make_shared<const model::Snapshot>(std::move(built)));
    }
}

} // namespace kalve137::studio
