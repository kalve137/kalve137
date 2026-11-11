// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/main_window.h"

#include "app/feature/feature_panel.h"
#include "app/feature/measure_panel.h"
#include "app/session/design_session.h"
#include "app/shell/marking_menu.h"
#include "app/theme/icons.h"
#include "app/theme/theme.h"
#include "app/view/scene.h"
#include "app/view/view_controls.h"
#include "core/naming.h"

#include "log/log.h"

#include <TopoDS.hxx>

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>

#include <algorithm>
#include <vector>

namespace kalve137::app {

namespace {

constexpr std::size_t kRecentCommandCount = 3;

}

void MainWindow::trackCommand(QAction* action)
{
    connect(action, &QAction::triggered, this, [this, action] {
        K137_INFO(App, "Command {}", action->text().remove('&').toStdString());
        std::erase(m_recentCommands, action);
        m_recentCommands.insert(m_recentCommands.begin(), action);
        if (m_recentCommands.size() > kRecentCommandCount) {
            m_recentCommands.pop_back();
        }
        updateRepeatAction();
    });
}

void MainWindow::repeatLastCommand()
{
    if (!m_recentCommands.empty()) {
        m_recentCommands.front()->trigger();
    }
}

void MainWindow::updateRepeatAction()
{
    if (m_recentCommands.empty()) {
        m_repeat->setText(tr("Repeat"));
        m_repeat->setEnabled(false);
        return;
    }
    const QAction* last = m_recentCommands.front();
    m_repeat->setText(tr("Repeat %1").arg(last->text().remove('&')));
    m_repeat->setIcon(last->icon());
    m_repeat->setEnabled(last->isEnabled());
}

void MainWindow::showMarkingMenu(QPoint center)
{
    using Slot = MarkingMenu::Slot;
    const bool sketching = m_sketchEditor->isActive();
    updateRepeatAction();

    auto* menu = new MarkingMenu(this);
    const auto temporary = [menu](const QIcon& icon, const QString& text, const QKeySequence& key, auto&& slot) {
        auto* action = new QAction(icon, text, menu);
        action->setShortcut(key);
        QObject::connect(action, &QAction::triggered, menu, slot);
        return action;
    };

    menu->setSlot(Slot::North, m_repeat);
    menu->setSlot(Slot::NorthEast, m_extrude);
    menu->setSlot(Slot::East, m_redo);
    menu->setSlot(Slot::SouthEast, m_fillet);
    menu->setSlot(Slot::West, m_undo);
    menu->setSlot(Slot::NorthWest, m_delete);
    if (sketching) {
        menu->setSlot(Slot::South, temporary(icons::tinted("finish_sketch", theme::kGold), m_finishSketch->text(), {},
                                             [this] { finishSketch(); }));
        menu->setSlot(Slot::SouthWest, m_toolActions.at(SketchEditor::Tool::Line));
    } else {
        menu->setSlot(Slot::South, m_createSketch);
        menu->setSlot(Slot::SouthWest, m_pressPull);
    }

    if (m_featurePanel->isActive()) {
        menu->addListAction(
            temporary({}, tr("OK"), QKeySequence(Qt::Key_Return), [this] { m_featurePanel->accept(); }));
    }
    const bool idle = !sketching && !m_featurePanel->isActive() && !m_measurePanel->isActive() && !m_pickingPlane;
    std::vector<Pick> picks;
    if (idle) {
        picks = m_scene->selected();
        if (picks.empty() && m_scene->selectHovered()) {
            picks = m_scene->selected();
            updateStatus();
        }
    }
    if (std::ranges::any_of(picks, [](const Pick& p) { return p.kind == Pick::Kind::Face; })) {
        for (QAction* action : {m_shell, m_offsetPlane, m_appearance}) {
            menu->addListAction(action);
        }
        menu->addListSeparator();
    } else if (!sketching && m_chamfer->isEnabled()) {
        menu->addListAction(m_chamfer);
        menu->addListSeparator();
    }
    if (const core::Body* body = picks.empty() ? nullptr : m_session->model().body(picks.front().body())) {
        const Pick& pick = picks.front();
        core::FeatureId source = 0;
        const TopAbs_ShapeEnum type = pick.shape.IsNull() ? TopAbs_SHAPE : pick.shape.ShapeType();
        if (type == TopAbs_FACE) {
            source = core::featureOfFace(*body, TopoDS::Face(pick.shape));
        } else if (type == TopAbs_EDGE) {
            source = core::featureOfEdge(*body, TopoDS::Edge(pick.shape));
        }
        if (const core::Feature* feature = m_session->document().find(source)) {
            menu->addListAction(temporary({}, tr("Edit %1").arg(QString::fromStdString(feature->name)), {},
                                          [this, source] { editFeature(source); }));
        }
        const core::BodyKey key = body->key();
        const QString name = body->name.empty() ? tr("Body") : QString::fromStdString(body->name);
        menu->addListAction(temporary({}, tr("Hide %1").arg(name), {}, [this, key] {
            m_scene->clearSelection();
            m_session->setBodyVisible(key, false);
            updateStatus();
        }));
        menu->addListSeparator();
    }
    if (m_featurePanel->isActive() || sketching || m_pickingPlane) {
        menu->addListAction(temporary({}, tr("Cancel"), QKeySequence(Qt::Key_Escape), [this] { escape(); }));
        menu->addListSeparator();
    }
    for (QAction* action : m_viewControls->canvasActions()) {
        if (action->isEnabled()) {
            menu->addListAction(action);
        }
    }
    menu->addListSeparator();
    for (QAction* action : m_recentCommands) {
        menu->addListAction(action);
    }
    menu->popup(center);
}

} // namespace kalve137::app
