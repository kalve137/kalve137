// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/document.h"
#include "core/shape_store.h"

#include <QDateTime>
#include <QLockFile>
#include <QObject>
#include <QString>

#include <filesystem>
#include <future>
#include <memory>
#include <optional>

class QTimer;

namespace kalve137::app {

class DesignSession;

struct Recovery {
    core::Document document;
    std::filesystem::path source;
    QString name;
    QDateTime savedAt;
    std::shared_ptr<QLockFile> lock;
    std::filesystem::path file;
};

class Autosave : public QObject {
    Q_OBJECT

public:
    explicit Autosave(DesignSession& session, QObject* parent = nullptr);
    ~Autosave() override;

    void discard();

    static std::optional<Recovery> findRecovery();
    static void remove(const Recovery& recovery);

private:
    void write();
    void waitForWrite();

    DesignSession& m_session;
    QTimer* m_timer = nullptr;
    std::filesystem::path m_file;
    std::unique_ptr<QLockFile> m_lock;
    core::ShapeStore m_shapes;
    std::future<void> m_writing;
};

} // namespace kalve137::app
