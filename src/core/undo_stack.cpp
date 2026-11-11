// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/undo_stack.h"

#include <utility>

namespace kalve137::core {

UndoStack::UndoStack(std::size_t limit) : m_limit(limit) {}

void UndoStack::push(std::string label, const Document& before)
{
    m_undo.push_back({std::move(label), before});
    if (m_undo.size() > m_limit) {
        m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
}

std::optional<std::string> UndoStack::undoLabel() const
{
    return m_undo.empty() ? std::nullopt : std::optional(m_undo.back().label);
}

std::optional<std::string> UndoStack::redoLabel() const
{
    return m_redo.empty() ? std::nullopt : std::optional(m_redo.back().label);
}

void UndoStack::undo(Document& current)
{
    if (m_undo.empty()) {
        return;
    }
    Entry entry = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back({entry.label, std::move(current)});
    current = std::move(entry.snapshot);
}

void UndoStack::redo(Document& current)
{
    if (m_redo.empty()) {
        return;
    }
    Entry entry = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back({entry.label, std::move(current)});
    current = std::move(entry.snapshot);
}

void UndoStack::clear()
{
    m_undo.clear();
    m_redo.clear();
}

} // namespace kalve137::core
