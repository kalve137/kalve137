// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/panels/browser_panel.h"

#include "app/session/design_session.h"
#include "app/theme/icons.h"
#include "app/theme/theme.h"

#include <QHeaderView>
#include <QMenu>
#include <QTreeWidgetItemIterator>

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace kalve137::app {

namespace {

constexpr int kKindRole = Qt::UserRole;
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kPartRole = Qt::UserRole + 2;
constexpr int kMaxExpandedChildren = 12;

core::BodyKey bodyKeyOf(const QTreeWidgetItem* item)
{
    return {item->data(0, kIdRole).toInt(), item->data(0, kPartRole).toInt()};
}

} // namespace

BrowserPanel::BrowserPanel(DesignSession& session, QWidget* parent) : QTreeWidget(parent), m_session(session)
{
    setHeaderHidden(true);
    setColumnCount(1);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setUniformRowHeights(true);
    connect(this, &QTreeWidget::itemChanged, this, &BrowserPanel::onItemChanged);
    connect(this, &QWidget::customContextMenuRequested, this, &BrowserPanel::onContextMenu);
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const auto kind = static_cast<ItemKind>(item->data(0, kKindRole).toInt());
        if (kind == ItemKind::Sketch || kind == ItemKind::Construction) {
            emit editRequested(item->data(0, kIdRole).toInt());
        }
    });
    connect(&session, &DesignSession::changed, this, &BrowserPanel::rebuild, Qt::QueuedConnection);
    connect(&session, &DesignSession::fileStateChanged, this, &BrowserPanel::rebuild, Qt::QueuedConnection);
    connect(&session, &DesignSession::visibilityChanged, this, &BrowserPanel::syncVisibility);
    rebuild();
}

void BrowserPanel::rebuild()
{
    m_rebuilding = true;
    clear();
    const core::Document& document = m_session.document();
    const core::Model& model = m_session.model();

    const QIcon folder = icons::fixed("folder", theme::kTitanium);
    auto* root = new QTreeWidgetItem(this, {m_session.displayName()});
    root->setData(0, kKindRole, static_cast<int>(ItemKind::Group));
    root->setIcon(0, icons::fixed("body", theme::kGold));

    const auto addCheckable = [&](QTreeWidgetItem* parent, const QString& text, ItemKind kind, int id, bool visible) {
        auto* item = new QTreeWidgetItem(parent, {text});
        switch (kind) {
        case ItemKind::Group:
            break;
        case ItemKind::Origin:
            item->setIcon(0, folder);
            break;
        case ItemKind::Body:
            item->setIcon(0, icons::fixed("body"));
            break;
        case ItemKind::Sketch:
            item->setIcon(0, icons::fixed("create_sketch", theme::kSketchFree));
            break;
        case ItemKind::Component:
            item->setIcon(0, icons::fixed("body", theme::kGold));
            break;
        case ItemKind::Construction:
            break;
        }
        item->setData(0, kKindRole, static_cast<int>(kind));
        item->setData(0, kIdRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
        return item;
    };

    addCheckable(root, tr("Origin"), ItemKind::Origin, 0, m_session.isOriginVisible());

    QTreeWidgetItem* construction = nullptr;
    for (const core::Feature& feature : document.features()) {
        if (!model.planes.contains(feature.id) && !model.axes.contains(feature.id) &&
            !model.points.contains(feature.id)) {
            continue;
        }
        if (construction == nullptr) {
            construction = new QTreeWidgetItem(root, {tr("Construction")});
            construction->setData(0, kKindRole, static_cast<int>(ItemKind::Group));
            construction->setIcon(0, folder);
        }
        QTreeWidgetItem* item = addCheckable(construction, QString::fromStdString(feature.name), ItemKind::Construction,
                                             feature.id, m_session.isVisible(feature.id));
        item->setIcon(0, icons::fixed(icons::featureIconName(feature.data), theme::kTitanium));
    }

    auto* bodies = new QTreeWidgetItem(root, {tr("Bodies")});
    bodies->setData(0, kKindRole, static_cast<int>(ItemKind::Group));
    bodies->setIcon(0, folder);
    std::map<std::pair<core::FeatureId, std::vector<std::string>>, QTreeWidgetItem*> components;
    for (const core::Body& body : model.bodies) {
        QTreeWidgetItem* parent = bodies;
        std::vector<std::string> prefix;
        for (const std::string& component : body.path) {
            prefix.push_back(component);
            auto [it, added] = components.try_emplace({body.id, prefix}, nullptr);
            if (added) {
                const QString name = component.empty() ? tr("Component") : QString::fromStdString(component);
                it->second = addCheckable(parent, name, ItemKind::Component, body.id, true);
            }
            parent = it->second;
        }
        const QString name = body.name.empty() ? tr("Body") : QString::fromStdString(body.name);
        QTreeWidgetItem* item =
            addCheckable(parent, name, ItemKind::Body, body.id, m_session.isBodyVisible(body.key()));
        item->setData(0, kPartRole, body.part);
    }
    for (int i = 0; i < bodies->childCount(); ++i) {
        syncComponent(bodies->child(i));
    }

    auto* sketches = new QTreeWidgetItem(root, {tr("Sketches")});
    sketches->setData(0, kKindRole, static_cast<int>(ItemKind::Group));
    sketches->setIcon(0, folder);
    for (const core::Feature& feature : document.features()) {
        if (!std::holds_alternative<core::SketchFeature>(feature.data) || document.isRolledBack(feature.id)) {
            continue;
        }
        QTreeWidgetItem* item = addCheckable(sketches, QString::fromStdString(feature.name), ItemKind::Sketch,
                                             feature.id, m_session.isVisible(feature.id));
        auto status = model.status.find(feature.id);
        if (status != model.status.end()) {
            switch (status->second.health) {
            case core::FeatureHealth::Warning:
            case core::FeatureHealth::Failed:
                item->setForeground(0, theme::kConflictText);
                item->setToolTip(0, QString::fromStdString(status->second.message));
                break;
            case core::FeatureHealth::Ok:
            case core::FeatureHealth::Suppressed:
            case core::FeatureHealth::RolledBack:
                break;
            }
        }
    }
    expandAll();
    for (const auto& [key, component] : components) {
        component->setExpanded(component->childCount() <= kMaxExpandedChildren);
    }
    m_rebuilding = false;
}

Qt::CheckState BrowserPanel::syncComponent(QTreeWidgetItem* component)
{
    if (static_cast<ItemKind>(component->data(0, kKindRole).toInt()) != ItemKind::Component) {
        return component->checkState(0);
    }
    bool anyShown = false;
    bool anyHidden = false;
    for (int i = 0; i < component->childCount(); ++i) {
        switch (syncComponent(component->child(i))) {
        case Qt::Checked:
            anyShown = true;
            break;
        case Qt::Unchecked:
            anyHidden = true;
            break;
        case Qt::PartiallyChecked:
            anyShown = true;
            anyHidden = true;
            break;
        }
    }
    const Qt::CheckState state = anyShown && anyHidden ? Qt::PartiallyChecked : anyShown ? Qt::Checked : Qt::Unchecked;
    component->setCheckState(0, state);
    return state;
}

std::vector<core::BodyKey> BrowserPanel::bodiesIn(const QTreeWidgetItem* component)
{
    std::vector<core::BodyKey> keys;
    for (int i = 0; i < component->childCount(); ++i) {
        const QTreeWidgetItem* child = component->child(i);
        if (static_cast<ItemKind>(child->data(0, kKindRole).toInt()) == ItemKind::Component) {
            std::ranges::copy(bodiesIn(child), std::back_inserter(keys));
        } else {
            keys.push_back(bodyKeyOf(child));
        }
    }
    return keys;
}

void BrowserPanel::syncVisibility()
{
    m_rebuilding = true;
    for (QTreeWidgetItemIterator it(this); *it != nullptr; ++it) {
        QTreeWidgetItem* item = *it;
        bool visible = false;
        switch (static_cast<ItemKind>(item->data(0, kKindRole).toInt())) {
        case ItemKind::Group:
        case ItemKind::Component:
            continue;
        case ItemKind::Origin:
            visible = m_session.isOriginVisible();
            break;
        case ItemKind::Body:
            visible = m_session.isBodyVisible(bodyKeyOf(item));
            break;
        case ItemKind::Sketch:
        case ItemKind::Construction:
            visible = m_session.isVisible(item->data(0, kIdRole).toInt());
            break;
        }
        item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
    }
    for (QTreeWidgetItemIterator it(this); *it != nullptr; ++it) {
        const QTreeWidgetItem* parent = (*it)->parent();
        if (parent != nullptr && static_cast<ItemKind>(parent->data(0, kKindRole).toInt()) == ItemKind::Group) {
            syncComponent(*it);
        }
    }
    m_rebuilding = false;
}

void BrowserPanel::onItemChanged(QTreeWidgetItem* item, int)
{
    if (m_rebuilding) {
        return;
    }
    const bool visible = item->checkState(0) == Qt::Checked;
    switch (static_cast<ItemKind>(item->data(0, kKindRole).toInt())) {
    case ItemKind::Group:
        return;
    case ItemKind::Component:
        m_session.setBodiesVisible(bodiesIn(item), visible);
        return;
    case ItemKind::Origin:
        m_session.setOriginVisible(visible);
        return;
    case ItemKind::Body:
        m_session.setBodyVisible(bodyKeyOf(item), visible);
        return;
    case ItemKind::Sketch:
    case ItemKind::Construction:
        m_session.setVisible(item->data(0, kIdRole).toInt(), visible);
        return;
    }
}

void BrowserPanel::onContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = itemAt(pos);
    if (item == nullptr) {
        return;
    }
    const auto kind = static_cast<ItemKind>(item->data(0, kKindRole).toInt());
    const core::FeatureId id = item->data(0, kIdRole).toInt();
    QMenu menu(this);
    QAction* edit = nullptr;
    switch (kind) {
    case ItemKind::Group:
    case ItemKind::Origin:
    case ItemKind::Body:
    case ItemKind::Component:
        break;
    case ItemKind::Sketch:
        edit = menu.addAction(tr("Edit Sketch"));
        break;
    case ItemKind::Construction:
        edit = menu.addAction(tr("Edit %1").arg(item->text(0)));
        break;
    }
    if (kind == ItemKind::Group) {
        return;
    }
    QAction* appearance = nullptr;
    if (kind == ItemKind::Body || kind == ItemKind::Component) {
        appearance = menu.addAction(tr("Appearance..."));
    }
    const bool visible = item->checkState(0) == Qt::Checked;
    QAction* toggle = menu.addAction(visible ? tr("Hide") : tr("Show"));
    QAction* chosen = menu.exec(viewport()->mapToGlobal(pos));
    if (chosen != nullptr && chosen == edit) {
        emit editRequested(id);
    } else if (chosen != nullptr && chosen == appearance) {
        emit appearanceRequested(kind == ItemKind::Body ? std::vector{bodyKeyOf(item)} : bodiesIn(item));
    } else if (chosen == toggle) {
        item->setCheckState(0, visible ? Qt::Unchecked : Qt::Checked);
    }
}

} // namespace kalve137::app
