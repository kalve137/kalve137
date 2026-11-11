// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_driver.h"

#include "app/feature/feature_panel.h"
#include "app/panels/timeline_widget.h"
#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/sketch/sketch_editor.h"
#include "core/recompute.h"
#include "geometry/shapes.h"
#include "log/log.h"
#include "sketch/dimension.h"
#include "sketch/profiles.h"
#include "viewer/occt_view_widget.h"

#include <NCollection_IndexedMap.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QWheelEvent>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <format>
#include <fstream>
#include <mutex>
#include <numbers>
#include <ranges>

namespace kalve137::uitest {

namespace {

constexpr QSize kWindowSize{1600, 1000};
constexpr std::size_t kLevelColumn = 24;
constexpr int kWheelStep = 120;
constexpr int kFrameMs = 16;
constexpr qint64 kAnimationLimitMs = 3000;
constexpr int kOriginPlanePixels = 80;
constexpr int kPlaneSearchStep = 8;
constexpr int kPlaneSearchDirections = 24;
constexpr double kParallelTolerance = 1e-6;

struct Problems {
    std::mutex mutex;
    std::vector<std::string> found;
    std::vector<std::string> allowed;
};

std::deque<DialogAnswer>& answers()
{
    static std::deque<DialogAnswer> queue;
    return queue;
}

Problems& problems()
{
    static Problems instance;
    return instance;
}

void record(std::string problem)
{
    Problems& p = problems();
    const std::scoped_lock lock(p.mutex);
    if (std::ranges::any_of(p.allowed,
                            [&](const std::string& fragment) { return problem.find(fragment) != std::string::npos; })) {
        return;
    }
    p.found.push_back(std::move(problem));
}

void recordLog(std::string_view line)
{
    if (line.size() <= kLevelColumn) {
        return;
    }
    const std::string_view level = line.substr(kLevelColumn);
    if (level.starts_with("warning") || level.starts_with("error") || level.starts_with("critical")) {
        std::string text(line);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
        record(std::move(text));
    }
}

QtMessageHandler g_previousHandler = nullptr;

void onQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        record("qt: " + message.toStdString());
        std::fprintf(stderr, "qt %s: %s\n", type == QtFatalMsg ? "fatal" : "warning", qPrintable(message));
        std::fflush(stderr);
    }
    if (g_previousHandler != nullptr) {
        g_previousHandler(type, context, message);
    }
}

std::deque<QString>& menuChoices()
{
    static std::deque<QString> queue;
    return queue;
}

QString itemsOf(const QMenu& menu)
{
    QStringList items;
    for (const QAction* action : menu.actions()) {
        if (!action->isSeparator()) {
            items << action->text().remove('&');
        }
    }
    return items.join(", ");
}

void answerMenu(QMenu& menu)
{
    if (menuChoices().empty()) {
        record("menu: " + itemsOf(menu).toStdString());
        QTimer::singleShot(0, &menu, [&menu] { menu.close(); });
        return;
    }
    const QString choice = menuChoices().front();
    menuChoices().pop_front();
    QTimer::singleShot(0, &menu, [&menu, choice] {
        for (QAction* action : menu.actions()) {
            if (action->text().remove('&') == choice && action->isEnabled()) {
                menu.setActiveAction(action);
                QTest::keyClick(&menu, Qt::Key_Return);
                return;
            }
        }
        record("menu has no enabled " + choice.toStdString() + " in " + itemsOf(menu).toStdString());
        menu.close();
    });
}

class OffScreen : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget == nullptr || !widget->isWindow()) {
            return false;
        }
        if (event->type() == QEvent::Polish) {
            widget->setAttribute(Qt::WA_DontShowOnScreen);
        } else if (auto* menu = qobject_cast<QMenu*>(widget); menu != nullptr && event->type() == QEvent::Show) {
            answerMenu(*menu);
        } else if (event->type() == QEvent::Show && widget->isModal() && !answers().empty()) {
            DialogAnswer answer = std::move(answers().front());
            answers().pop_front();
            QTimer::singleShot(0, widget, [widget, answer = std::move(answer)] { answer(*widget); });
        } else if (event->type() == QEvent::Show && widget->isModal()) {
            QString text = widget->windowTitle();
            if (const auto* box = qobject_cast<QMessageBox*>(widget)) {
                text += ": " + box->text();
            }
            record("dialog: " + text.toStdString());
            QTimer::singleShot(0, widget, [widget] {
                if (auto* dialog = qobject_cast<QDialog*>(widget)) {
                    dialog->reject();
                } else {
                    widget->close();
                }
            });
        }
        return false;
    }
};

std::filesystem::path shotFolder()
{
    const QByteArray custom = qgetenv("KALVE137_UI_SHOTS");
    std::filesystem::path root =
        custom.isEmpty() ? std::filesystem::path(QCoreApplication::applicationDirPath().toStdU16String()) / "ui-shots"
                         : std::filesystem::path(QString::fromLocal8Bit(custom).toStdU16String());
    if (const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info()) {
        root /= std::format("{}.{}", test->test_suite_name(), test->name());
    }
    return root;
}

std::string healthName(core::FeatureHealth health)
{
    switch (health) {
    case core::FeatureHealth::Ok:
        return "ok";
    case core::FeatureHealth::Warning:
        return "warning";
    case core::FeatureHealth::Failed:
        return "failed";
    case core::FeatureHealth::Suppressed:
        return "suppressed";
    case core::FeatureHealth::RolledBack:
        return "rolled back";
    }
    return "ok";
}

int count(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> map;
    TopExp::MapShapes(shape, type, map);
    return map.Extent();
}

double measured(const sketch::Sketch& s, const sketch::Constraint& c)
{
    if (const auto ends = sketch::measuredPoints(s, c)) {
        const sketch::Vec2 d = ends->second - ends->first;
        if (c.kind == sketch::ConstraintKind::HorizontalDistance) {
            return std::abs(d.x);
        }
        if (c.kind == sketch::ConstraintKind::VerticalDistance) {
            return std::abs(d.y);
        }
        return std::hypot(d.x, d.y);
    }
    if (const sketch::Curve* curve = c.refs.empty() ? nullptr : s.curve(c.refs[0]); curve != nullptr) {
        return c.kind == sketch::ConstraintKind::Diameter ? curve->radius * 2.0 : curve->radius;
    }
    return c.value;
}

nlohmann::json dimensionsOf(const sketch::Sketch& s, const gp_Ax3& plane)
{
    nlohmann::json dimensions = nlohmann::json::array();
    for (const auto& [id, c] : s.constraints()) {
        if (!sketch::isDimension(c.kind)) {
            continue;
        }
        nlohmann::json entry{{"id", id},
                             {"kind", std::string(sketch::toString(c.kind))},
                             {"value", c.value},
                             {"measured", measured(s, c)}};
        if (const std::optional<sketch::Vec2> label = sketch::labelPosition(s, c)) {
            const gp_Pnt at = sketch::toWorld(plane, *label);
            entry["label"] = {at.X(), at.Y(), at.Z()};
        }
        dimensions.push_back(std::move(entry));
    }
    return dimensions;
}

} // namespace

void installHarness()
{
    QStandardPaths::setTestModeEnabled(true);
    g_previousHandler = qInstallMessageHandler(onQtMessage);
    log::Options options;
    options.debuggerOutput = recordLog;
    log::init(options);
    qApp->installEventFilter(new OffScreen(qApp));
}

void resetSettings()
{
    QSettings().clear();
    for (const QStandardPaths::StandardLocation location :
         {QStandardPaths::AppLocalDataLocation, QStandardPaths::AppDataLocation}) {
        if (const QString folder = QStandardPaths::writableLocation(location); !folder.isEmpty()) {
            QDir(folder).removeRecursively();
        }
    }
}

std::vector<std::string> takeProblems()
{
    Problems& p = problems();
    const std::scoped_lock lock(p.mutex);
    std::vector<std::string> found = std::move(p.found);
    p.found.clear();
    p.allowed.clear();
    return found;
}

void allowProblem(const std::string& fragment)
{
    Problems& p = problems();
    const std::scoped_lock lock(p.mutex);
    p.allowed.push_back(fragment);
}

void answerDialog(DialogAnswer answer)
{
    answers().push_back(std::move(answer));
}

void answerFile(const std::filesystem::path& path)
{
    answerDialog([path](QWidget& widget) {
        auto* dialog = qobject_cast<QFileDialog*>(&widget);
        if (dialog == nullptr) {
            record("dialog: expected a file dialog, got " + widget.windowTitle().toStdString());
            widget.close();
            return;
        }
        dialog->selectFile(QString::fromStdU16String(path.u16string()));
        static_cast<QDialog*>(dialog)->accept();
    });
}

void answerMessage(QMessageBox::StandardButton button)
{
    answerDialog([button](QWidget& widget) {
        auto* box = qobject_cast<QMessageBox*>(&widget);
        QAbstractButton* choice = box != nullptr ? box->button(button) : nullptr;
        if (choice == nullptr) {
            record("dialog: expected a message box with the button, got " + widget.windowTitle().toStdString());
            widget.close();
            return;
        }
        choice->click();
    });
}

void answerText(const QString& text)
{
    answerDialog([text](QWidget& widget) {
        auto* dialog = qobject_cast<QInputDialog*>(&widget);
        if (dialog == nullptr) {
            record("dialog: expected a text input, got " + widget.windowTitle().toStdString());
            widget.close();
            return;
        }
        dialog->setTextValue(text);
        dialog->accept();
    });
}

void chooseFromMenu(const QString& item)
{
    menuChoices().push_back(item);
}

std::size_t pendingAnswers()
{
    const std::size_t count = answers().size() + menuChoices().size();
    answers().clear();
    menuChoices().clear();
    return count;
}

UiDriver::UiDriver() : m_window(std::make_unique<app::MainWindow>())
{
    std::error_code ignored;
    std::filesystem::remove_all(shotFolder(), ignored);
    m_window->setAttribute(Qt::WA_DontShowOnScreen);
    m_window->resize(kWindowSize);
    m_window->show();
    keepActive();
    m_view = m_window->findChild<viewer::OcctViewWidget*>();
    m_session = m_window->findChild<app::DesignSession*>();
    EXPECT_NE(m_view, nullptr);
    EXPECT_NE(m_session, nullptr);
    settle();
    EXPECT_TRUE(m_view->isReady()) << "The view did not initialize off screen";
    settle();
}

UiDriver::~UiDriver()
{
    m_window.reset();
    QCoreApplication::processEvents();
}

const core::Document& UiDriver::document() const
{
    return m_session->document();
}

QAction* UiDriver::action(const QString& text) const
{
    const QList<QAction*> actions = m_window->findChildren<QAction*>();
    for (QAction* candidate : actions) {
        if (candidate->text().remove('&') == text) {
            return candidate;
        }
    }
    for (QAction* candidate : actions) {
        if (candidate->text().remove('&').startsWith(text + ' ')) {
            return candidate;
        }
    }
    return nullptr;
}

void UiDriver::trigger(const QString& text)
{
    QAction* found = action(text);
    if (found == nullptr) {
        ADD_FAILURE() << "No action " << text.toStdString();
        return;
    }
    if (!found->isEnabled()) {
        ADD_FAILURE() << "Action " << text.toStdString() << " is disabled";
        return;
    }
    found->trigger();
    settle();
}

void UiDriver::keepActive()
{
    if (QApplication::activeModalWidget() == nullptr && QApplication::activeWindow() != m_window.get()) {
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QApplication::setActiveWindow(m_window.get());
        QT_WARNING_POP
    }
}

void UiDriver::frame()
{
    keepActive();
    QCoreApplication::processEvents();
    if (m_view != nullptr) {
        m_view->grabFramebuffer();
    }
    QCoreApplication::processEvents();
}

void UiDriver::settle(int milliseconds)
{
    if (milliseconds > 0) {
        QTest::qWait(milliseconds);
    }
    frame();
    QElapsedTimer clock;
    clock.start();
    while (m_view != nullptr && m_view->isAnimating() && clock.elapsed() < kAnimationLimitMs) {
        QTest::qWait(kFrameMs);
        frame();
    }
    frame();
}

QPointF UiDriver::originPlane(core::OriginPlane plane)
{
    const gp_Dir normal = core::originPlane(plane).Direction();
    const QPointF origin = at(gp::Origin());
    for (int radius = kPlaneSearchStep; radius <= kOriginPlanePixels; radius += kPlaneSearchStep) {
        for (int step = 0; step < kPlaneSearchDirections; ++step) {
            const double angle = 2.0 * std::numbers::pi * step / kPlaneSearchDirections;
            const QPointF candidate = origin + QPointF(std::cos(angle), std::sin(angle)) * radius;
            move(candidate);
            if (m_view->gridPlane().Direction().IsParallel(normal, kParallelTolerance) &&
                m_view->aisContext()->HasDetected()) {
                return candidate;
            }
        }
    }
    ADD_FAILURE() << "No spot hovers origin plane " << core::toString(plane);
    return origin;
}

QPointF UiDriver::at(const gp_Pnt& world) const
{
    return m_view->toScreen(world);
}

void UiDriver::sendMouse(QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
                         Qt::KeyboardModifiers modifiers)
{
    const QPoint inWindow = m_view->mapTo(m_window.get(), pos.toPoint());
    QWidget* target = m_grab;
    if (target == nullptr) {
        target = m_window->childAt(inWindow);
    }
    if (target == nullptr) {
        target = m_view;
    }
    if (type == QEvent::MouseButtonPress) {
        m_grab = target;
        for (QWidget* w = target; w != nullptr; w = w->parentWidget()) {
            if (w->isEnabled() && (w->focusPolicy() & Qt::ClickFocus) == Qt::ClickFocus) {
                if (w != QApplication::focusWidget()) {
                    w->setFocus(Qt::MouseFocusReason);
                }
                break;
            }
            if (w->isWindow()) {
                break;
            }
        }
    } else if (type == QEvent::MouseButtonRelease && buttons == Qt::NoButton) {
        m_grab = nullptr;
    }
    const QPointF local = QPointF(target->mapFrom(m_window.get(), inWindow)) + (pos - pos.toPoint());
    QMouseEvent event(type, local, target->mapToGlobal(local), button, buttons, modifiers);
    QApplication::sendEvent(target, &event);
}

QPointF UiDriver::centerOf(const QWidget* widget) const
{
    return inView(widget, widget->rect().center());
}

QPointF UiDriver::inView(const QWidget* widget, QPoint local) const
{
    return m_view->mapFrom(m_window.get(), widget->mapTo(m_window.get(), local));
}

QPointF UiDriver::timelineChip(core::FeatureId id) const
{
    const auto* timeline = m_window->findChild<app::TimelineWidget*>();
    const std::optional<QRect> rect = timeline != nullptr ? timeline->chipRect(id) : std::nullopt;
    if (!rect) {
        ADD_FAILURE() << "No timeline chip for feature " << id;
        return {};
    }
    return inView(timeline, rect->center());
}

QPointF UiDriver::browserItem(const QString& text)
{
    for (auto* tree : m_window->findChildren<QTreeWidget*>()) {
        if (!tree->isVisible()) {
            continue;
        }
        const QList<QTreeWidgetItem*> found = tree->findItems(text, Qt::MatchExactly | Qt::MatchRecursive);
        if (!found.isEmpty()) {
            for (QTreeWidgetItem* parent = found.front()->parent(); parent != nullptr; parent = parent->parent()) {
                parent->setExpanded(true);
            }
            settle();
            tree->scrollToItem(found.front());
            settle();
            return inView(tree->viewport(), tree->visualItemRect(found.front()).center());
        }
    }
    ADD_FAILURE() << "No browser item " << text.toStdString();
    return {};
}

void UiDriver::contextMenu(QPointF pos, const QString& item)
{
    chooseFromMenu(item);
    move(pos);
    const QPoint inWindow = m_view->mapTo(m_window.get(), pos.toPoint());
    QWidget* target = m_window->childAt(inWindow);
    if (target == nullptr) {
        ADD_FAILURE() << "Nothing to right-click at " << pos.x() << "," << pos.y();
        return;
    }
    const QPoint local = target->mapFrom(m_window.get(), inWindow);
    QContextMenuEvent event(QContextMenuEvent::Mouse, local, target->mapToGlobal(local));
    QApplication::sendEvent(target, &event);
    settle();
}

void UiDriver::click(const QWidget* widget)
{
    if (widget == nullptr || !widget->isVisible()) {
        ADD_FAILURE() << "Clicked a missing or hidden widget";
        return;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    for (const QWidget* w = widget; w != nullptr; w = w->parentWidget()) {
        if (w->layout() != nullptr) {
            w->layout()->activate();
        }
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    click(centerOf(widget));
}

QWidget* UiDriver::field(const QString& label) const
{
    for (QFormLayout* form : m_window->findChildren<QFormLayout*>()) {
        if (form->parentWidget() == nullptr || !form->parentWidget()->isVisible()) {
            continue;
        }
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem* labelItem = form->itemAt(row, QFormLayout::LabelRole);
            QLayoutItem* fieldItem = form->itemAt(row, QFormLayout::FieldRole);
            const auto* text = labelItem != nullptr ? qobject_cast<QLabel*>(labelItem->widget()) : nullptr;
            if (text == nullptr || fieldItem == nullptr || text->text().remove('&') != label) {
                continue;
            }
            if (QWidget* widget = fieldItem->widget()) {
                if (qobject_cast<QLineEdit*>(widget) != nullptr || qobject_cast<QComboBox*>(widget) != nullptr ||
                    qobject_cast<QAbstractButton*>(widget) != nullptr) {
                    return widget;
                }
                if (auto* edit = widget->findChild<QLineEdit*>()) {
                    return edit;
                }
                return widget;
            }
        }
    }
    return nullptr;
}

void UiDriver::fill(const QString& label, const QString& text, bool accept)
{
    QWidget* widget = field(label);
    if (widget == nullptr) {
        ADD_FAILURE() << "No field " << label.toStdString();
        return;
    }
    click(widget);
    if (focused() != widget) {
        const QPoint local = m_window->mapFromGlobal(widget->mapToGlobal(widget->rect().center()));
        const QWidget* hit = m_window->childAt(local);
        ADD_FAILURE() << label.toStdString() << " field " << widget->metaObject()->className() << " at " << local.x()
                      << "," << local.y() << " size " << widget->width() << "x" << widget->height() << " sits under "
                      << (hit != nullptr ? hit->metaObject()->className() : "nothing")
                      << (hit != nullptr ? hit->objectName().toStdString() : std::string());
    }
    enterValue(text, accept);
}

void UiDriver::choose(const QString& label, const QString& item)
{
    auto* box = qobject_cast<QComboBox*>(field(label));
    if (box == nullptr) {
        ADD_FAILURE() << "No choice field " << label.toStdString();
        return;
    }
    const int index = box->findText(item);
    if (index < 0) {
        ADD_FAILURE() << label.toStdString() << " has no item " << item.toStdString();
        return;
    }
    box->setCurrentIndex(index);
    settle();
}

QAbstractButton* UiDriver::button(const QString& text) const
{
    for (QAbstractButton* candidate : m_window->findChildren<QAbstractButton*>()) {
        if (candidate->isVisible() && candidate->text().remove('&') == text) {
            return candidate;
        }
    }
    return nullptr;
}

void UiDriver::pressButton(const QString& text)
{
    QAbstractButton* found = button(text);
    if (found == nullptr) {
        ADD_FAILURE() << "No visible button " << text.toStdString();
        return;
    }
    if (!found->isEnabled()) {
        ADD_FAILURE() << "Button " << text.toStdString() << " is disabled";
        return;
    }
    click(found);
}

void UiDriver::move(QPointF pos, Qt::MouseButtons buttons)
{
    sendMouse(QEvent::MouseMove, pos, Qt::NoButton, buttons, {});
    frame();
}

void UiDriver::press(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    m_buttons |= button;
    sendMouse(QEvent::MouseButtonPress, pos, button, m_buttons, modifiers);
    frame();
}

void UiDriver::release(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    m_buttons &= ~Qt::MouseButtons(button);
    sendMouse(QEvent::MouseButtonRelease, pos, button, m_buttons, modifiers);
    frame();
}

void UiDriver::click(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    move(pos);
    move(pos);
    press(pos, button, modifiers);
    release(pos, button, modifiers);
    settle();
}

void UiDriver::click(const gp_Pnt& world, Qt::KeyboardModifiers modifiers)
{
    click(at(world), Qt::LeftButton, modifiers);
}

void UiDriver::doubleClick(QPointF pos)
{
    click(pos);
    m_buttons |= Qt::LeftButton;
    sendMouse(QEvent::MouseButtonDblClick, pos, Qt::LeftButton, m_buttons, {});
    release(pos);
    settle();
}

void UiDriver::drag(QPointF from, QPointF to, Qt::MouseButton button, Qt::KeyboardModifiers modifiers, int steps)
{
    move(from);
    press(from, button, modifiers);
    for (int i = 1; i <= steps; ++i) {
        move(from + (to - from) * (static_cast<double>(i) / steps), m_buttons);
    }
    release(to, button, modifiers);
    settle();
}

void UiDriver::wheel(QPointF pos, int notches)
{
    QWheelEvent event(pos, m_view->mapToGlobal(pos), QPoint(), QPoint(0, kWheelStep * notches), m_buttons, {},
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(m_view, &event);
    settle();
}

QWidget* UiDriver::focused() const
{
    QWidget* widget = m_window->focusWidget();
    return widget != nullptr ? widget : m_view;
}

void UiDriver::key(int key, Qt::KeyboardModifiers modifiers)
{
    QTest::keyClick(focused(), static_cast<Qt::Key>(key), modifiers);
    settle();
}

void UiDriver::type(const QString& text)
{
    QTest::keyClicks(focused(), text);
    settle();
}

void UiDriver::enterValue(const QString& text, bool accept)
{
    QWidget* target = focused();
    if (auto* edit = qobject_cast<QLineEdit*>(target)) {
        edit->selectAll();
    } else {
        ADD_FAILURE() << "No value field has focus, " << target->metaObject()->className() << " does";
    }
    QTest::keyClicks(target, text);
    QTest::keyClick(target, accept ? Qt::Key_Return : Qt::Key_Tab);
    settle();
}

QString UiDriver::statusText() const
{
    return m_window->statusBar()->currentMessage();
}

QString UiDriver::selectionText() const
{
    const QList<QLabel*> labels = m_window->statusBar()->findChildren<QLabel*>(Qt::FindDirectChildrenOnly);
    return labels.isEmpty() ? QString() : labels.front()->text();
}

QImage UiDriver::shot()
{
    settle();
    return m_window->grab().toImage();
}

std::filesystem::path UiDriver::save(const std::string& name)
{
    const std::filesystem::path folder = shotFolder();
    std::filesystem::create_directories(folder);
    const std::filesystem::path path = folder / std::format("{:02}-{}.png", ++m_shotIndex, name);
    shot().save(QString::fromStdU16String(path.u16string()));
    std::ofstream(folder / "state.json") << state().dump(2);
    return path;
}

nlohmann::json UiDriver::state() const
{
    nlohmann::json features = nlohmann::json::array();
    const core::Model& model = m_session->model();
    for (const core::Feature& feature : document().features()) {
        nlohmann::json entry{{"id", feature.id},
                             {"name", feature.name},
                             {"type", std::string(core::typeName(feature.data))},
                             {"suppressed", feature.suppressed}};
        if (auto it = model.status.find(feature.id); it != model.status.end()) {
            entry["health"] = healthName(it->second.health);
            entry["message"] = it->second.message;
        }
        features.push_back(std::move(entry));
    }
    nlohmann::json bodies = nlohmann::json::array();
    for (const core::Body& body : model.bodies) {
        bodies.push_back({{"name", body.name},
                          {"feature", body.id},
                          {"part", body.part},
                          {"visible", m_session->isBodyVisible(body.key())},
                          {"volume", geometry::volume(body.shape)},
                          {"faces", count(body.shape, TopAbs_FACE)},
                          {"edges", count(body.shape, TopAbs_EDGE)}});
    }
    const auto* editor = m_window->findChild<app::SketchEditor*>();
    const auto* panel = m_window->findChild<app::FeaturePanel*>();
    const bool sketching = editor != nullptr && editor->isActive();
    return {
        {"dimensions", sketching ? dimensionsOf(editor->shownSketch(), m_session->sketchPlane(editor->sketchId()))
                                 : nlohmann::json::array()},
        {"title", m_window->windowTitle().toStdString()},
        {"dirty", m_session->isDirty()},
        {"features", std::move(features)},
        {"bodies", std::move(bodies)},
        {"sketch", editor != nullptr && editor->isActive() ? editor->sketchId() : 0},
        {"featurePanel", panel != nullptr && panel->isActive()},
        {"status", statusText().toStdString()},
        {"selection", selectionText().toStdString()},
        {"focus", focused()->metaObject()->className()},
    };
}

} // namespace kalve137::uitest
