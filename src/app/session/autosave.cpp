// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/autosave.h"

#include "app/session/design_session.h"
#include "app/session/paths.h"

#include "core/serialization.h"
#include "log/log.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <chrono>
#include <exception>
#include <system_error>

namespace kalve137::app {

namespace {

constexpr int kIntervalMs = 30000;
constexpr int kBusyRetryMs = 2000;
const char* const kSourceKey = "source";
const char* const kNameKey = "name";

std::filesystem::path recoveryFolder()
{
    return toPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)) / "recovery";
}

std::filesystem::path withExtension(std::filesystem::path file, const char* extension)
{
    return file.replace_extension(extension);
}

std::unique_ptr<QLockFile> makeLock(const std::filesystem::path& file)
{
    auto lock = std::make_unique<QLockFile>(toQString(withExtension(file, ".lock")));
    lock->setStaleLockTime(0);
    return lock;
}

void removeFiles(const std::filesystem::path& file)
{
    std::error_code error;
    std::filesystem::remove(file, error);
    std::filesystem::remove(withExtension(file, ".json"), error);
    std::filesystem::remove_all(withExtension(file, ".shapes"), error);
}

} // namespace

Autosave::Autosave(DesignSession& session, QObject* parent) : QObject(parent), m_session(session)
{
    const std::filesystem::path folder = recoveryFolder();
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    m_file = folder / toPath(QUuid::createUuid().toString(QUuid::WithoutBraces) + core::kDesignExtension);
    m_lock = makeLock(m_file);
    m_lock->tryLock(0);
    m_shapes.setFolder(withExtension(m_file, ".shapes"));

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(kIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &Autosave::write);
    connect(&m_session, &DesignSession::fileStateChanged, this, [this] {
        if (!m_session.isDirty()) {
            discard();
        } else if (!m_timer->isActive()) {
            m_timer->start();
        }
    });
}

Autosave::~Autosave()
{
    waitForWrite();
}

void Autosave::discard()
{
    m_timer->stop();
    waitForWrite();
    removeFiles(m_file);
}

void Autosave::waitForWrite()
{
    if (m_writing.valid()) {
        m_writing.wait();
    }
}

void Autosave::write()
{
    if (!m_session.isDirty()) {
        discard();
        return;
    }
    m_timer->setInterval(kIntervalMs);
    if (m_writing.valid() && m_writing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        m_timer->start(kBusyRetryMs);
        return;
    }
    m_writing = std::async(std::launch::async, [this, document = m_session.document(),
                                                meta = QJsonObject{{kSourceKey, toQString(m_session.path())},
                                                                   {kNameKey, m_session.displayName()}}] {
        try {
            core::saveDesign(document, m_file, m_shapes);
            m_shapes.prune(core::peekDesign(m_file).shapes);
            QFile file(toQString(withExtension(m_file, ".json")));
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(QJsonDocument(meta).toJson());
            }
        } catch (const std::exception& error) {
            K137_WARN(Io, "Autosave failed: {}", error.what());
        }
    });
}

std::optional<Recovery> Autosave::findRecovery()
{
    std::optional<Recovery> newest;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(recoveryFolder(), error)) {
        if (!entry.is_regular_file(error)) {
            continue;
        }
        if (entry.path().extension() == ".lock") {
            if (!std::filesystem::exists(withExtension(entry.path(), core::kDesignExtension), error)) {
                makeLock(entry.path())->tryLock(0);
            }
            continue;
        }
        if (entry.path().extension() != core::kDesignExtension) {
            continue;
        }
        std::shared_ptr<QLockFile> lock = makeLock(entry.path());
        if (!lock->tryLock(0)) {
            continue;
        }
        const QDateTime savedAt = QFileInfo(toQString(entry.path())).lastModified();
        if (newest && newest->savedAt >= savedAt) {
            continue;
        }
        Recovery recovery;
        try {
            core::ShapeStore shapes(withExtension(entry.path(), ".shapes"));
            recovery.document = core::loadDesign(entry.path(), shapes);
        } catch (const std::exception&) {
            removeFiles(entry.path());
            continue;
        }
        QFile file(toQString(withExtension(entry.path(), ".json")));
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject meta = QJsonDocument::fromJson(file.readAll()).object();
            recovery.source = toPath(meta.value(kSourceKey).toString());
            recovery.name = meta.value(kNameKey).toString();
        }
        recovery.savedAt = savedAt;
        recovery.lock = std::move(lock);
        recovery.file = entry.path();
        newest = std::move(recovery);
    }
    return newest;
}

void Autosave::remove(const Recovery& recovery)
{
    removeFiles(recovery.file);
}

} // namespace kalve137::app
