// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/document.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace kalve137::core {

class UndoStack {
public:
    explicit UndoStack(std::size_t limit = 200);

    void push(std::string label, const Document& before);

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    std::optional<std::string> undoLabel() const;
    std::optional<std::string> redoLabel() const;

    void undo(Document& current);
    void redo(Document& current);
    void clear();

private:
    struct Entry {
        std::string label;
        Document snapshot;
    };

    std::vector<Entry> m_undo;
    std::vector<Entry> m_redo;
    std::size_t m_limit;
};

} // namespace kalve137::core
