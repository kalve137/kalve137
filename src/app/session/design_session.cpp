// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"

#include "app/session/paths.h"

#include "log/log.h"

#include <variant>

namespace kalve137::app {

namespace {

bool usesSketch(const core::FeatureData& data, core::FeatureId sketch)
{
    return std::visit(core::Overloaded{
                          [](const core::SketchFeature&) { return false; },
                          [&](const core::ExtrudeFeature& f) { return f.profiles.sketch == sketch; },
                          [&](const core::RevolveFeature& f) {
                              return f.profiles.sketch == sketch ||
                                     (f.axis.kind == core::AxisKind::SketchLine && f.axis.sketch == sketch);
                          },
                          [](const core::FilletFeature&) { return false; },
                          [](const core::ChamferFeature&) { return false; },
                          [](const core::ShellFeature&) { return false; },
                          [](const core::PatternFeature&) { return false; },
                          [](const core::ImportFeature&) { return false; },
                          [](const core::PlaneFeature&) { return false; },
                          [](const core::OffsetFaceFeature&) { return false; },
                          [&](const core::HoleFeature& f) { return f.sketch == sketch; },
                          [](const core::MoveFeature&) { return false; },
                          [](const core::AxisFeature&) { return false; },
                          [](const core::PointFeature&) { return false; },
                          [](const core::AppearanceFeature&) { return false; },
                      },
                      data);
}

} // namespace

DesignSession::DesignSession(QObject* parent) : QObject(parent)
{
    rebuild();
}

void DesignSession::edit(const QString& label, const std::function<void(core::Document&)>& change)
{
    const core::Document before = m_document;
    change(m_document);
    K137_INFO(App, "Edit: {}", label.toStdString());
    m_undo.push(label.toStdString(), before);
    m_dirty = true;
    rebuild();
    emit fileStateChanged();
}

QString DesignSession::undoLabel() const
{
    return QString::fromStdString(m_undo.undoLabel().value_or(std::string{}));
}

QString DesignSession::redoLabel() const
{
    return QString::fromStdString(m_undo.redoLabel().value_or(std::string{}));
}

void DesignSession::undo()
{
    if (!m_undo.canUndo()) {
        return;
    }
    K137_INFO(App, "Undo: {}", m_undo.undoLabel().value_or(std::string{}));
    m_undo.undo(m_document);
    m_dirty = true;
    rebuild();
    emit fileStateChanged();
}

void DesignSession::redo()
{
    if (!m_undo.canRedo()) {
        return;
    }
    K137_INFO(App, "Redo: {}", m_undo.redoLabel().value_or(std::string{}));
    m_undo.redo(m_document);
    m_dirty = true;
    rebuild();
    emit fileStateChanged();
}

void DesignSession::reset(core::Document document, std::filesystem::path path)
{
    m_document = std::move(document);
    m_path = std::move(path);
    K137_INFO(App, "Design reset to {} with {} features", m_path.empty() ? "a new design" : log::utf8(m_path),
              m_document.features().size());
    m_undo.clear();
    m_dirty = false;
    m_visibility.clear();
    m_hiddenBodies.clear();
    rebuild();
    emit fileStateChanged();
    emit visibilityChanged();
}

void DesignSession::recover(core::Document document, std::filesystem::path path)
{
    reset(std::move(document), std::move(path));
    m_dirty = true;
    emit fileStateChanged();
}

void DesignSession::markSaved(std::filesystem::path path)
{
    m_path = std::move(path);
    m_dirty = false;
    emit fileStateChanged();
}

QString DesignSession::displayName() const
{
    if (m_path.empty()) {
        return tr("Untitled");
    }
    return toQString(m_path.stem());
}

gp_Ax3 DesignSession::sketchPlane(core::FeatureId sketch) const
{
    if (auto state = m_model.sketches.find(sketch); state != m_model.sketches.end()) {
        return state->second.plane;
    }
    const core::Feature* feature = m_document.find(sketch);
    const auto* data = feature != nullptr ? std::get_if<core::SketchFeature>(&feature->data) : nullptr;
    return data != nullptr ? data->plane : core::originPlane(core::OriginPlane::XY);
}

bool DesignSession::isVisible(core::FeatureId id) const
{
    if (auto it = m_visibility.find(id); it != m_visibility.end()) {
        return it->second;
    }
    const core::Feature* feature = m_document.find(id);
    if (feature == nullptr || !std::holds_alternative<core::SketchFeature>(feature->data)) {
        return true;
    }
    for (const core::Feature& other : m_document.features()) {
        if (other.id == id || other.suppressed || m_document.isRolledBack(other.id)) {
            continue;
        }
        if (m_document.indexOf(other.id) > m_document.indexOf(id) && usesSketch(other.data, id)) {
            return false;
        }
    }
    return true;
}

void DesignSession::setVisible(core::FeatureId id, bool visible)
{
    if (isVisible(id) == visible) {
        return;
    }
    m_visibility[id] = visible;
    emit visibilityChanged();
}

void DesignSession::setBodyVisible(core::BodyKey key, bool visible)
{
    setBodiesVisible({key}, visible);
}

void DesignSession::setBodiesVisible(const std::vector<core::BodyKey>& keys, bool visible)
{
    bool changedAny = false;
    for (const core::BodyKey& key : keys) {
        if (isBodyVisible(key) == visible) {
            continue;
        }
        if (visible) {
            m_hiddenBodies.erase(key);
        } else {
            m_hiddenBodies.insert(key);
        }
        changedAny = true;
    }
    if (changedAny) {
        emit visibilityChanged();
    }
}

void DesignSession::setOriginVisible(bool visible)
{
    if (m_originVisible == visible) {
        return;
    }
    m_originVisible = visible;
    emit visibilityChanged();
}

void DesignSession::rebuild()
{
    m_model = core::recompute(m_document);
    for (const core::Feature& feature : m_document.features()) {
        const core::FeatureStatus& status = m_model.status.at(feature.id);
        if (status.health == core::FeatureHealth::Failed) {
            K137_WARN(Core, "{} failed: {}", feature.name, status.message);
        }
    }
    emit changed();
}

} // namespace kalve137::app
