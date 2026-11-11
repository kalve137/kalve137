// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/document.h"
#include "core/recompute.h"
#include "core/undo_stack.h"

#include <QObject>
#include <QString>

#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace kalve137::app {

class DesignSession : public QObject {
    Q_OBJECT

public:
    explicit DesignSession(QObject* parent = nullptr);

    const core::Document& document() const { return m_document; }
    const core::Model& model() const { return m_model; }
    gp_Ax3 sketchPlane(core::FeatureId sketch) const;

    void edit(const QString& label, const std::function<void(core::Document&)>& change);
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    QString undoLabel() const;
    QString redoLabel() const;
    void undo();
    void redo();

    void reset(core::Document document, std::filesystem::path path);
    void recover(core::Document document, std::filesystem::path path);
    const std::filesystem::path& path() const { return m_path; }
    bool isDirty() const { return m_dirty; }
    void markSaved(std::filesystem::path path);
    QString displayName() const;

    bool isVisible(core::FeatureId id) const;
    void setVisible(core::FeatureId id, bool visible);
    bool isBodyVisible(core::BodyKey key) const { return !m_hiddenBodies.contains(key); }
    void setBodyVisible(core::BodyKey key, bool visible);
    void setBodiesVisible(const std::vector<core::BodyKey>& keys, bool visible);
    bool isOriginVisible() const { return m_originVisible; }
    void setOriginVisible(bool visible);

signals:
    void changed();
    void fileStateChanged();
    void visibilityChanged();

private:
    void rebuild();

    core::Document m_document;
    core::Model m_model;
    core::UndoStack m_undo;
    std::filesystem::path m_path;
    bool m_dirty = false;
    std::map<core::FeatureId, bool> m_visibility;
    std::set<core::BodyKey> m_hiddenBodies;
    bool m_originVisible = false;
};

} // namespace kalve137::app
