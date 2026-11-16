// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lang/check.h"
#include "lang/edit.h"
#include "lang/format.h"
#include "lang/parse.h"
#include "look/image.h"
#include "model/select.h"
#include "studio/rebuilder.h"
#include "studio/view_widget.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>

namespace {

using namespace kalve137;

constexpr int kDebounceMs = 90;
constexpr double kDrillDiameter = 3.0;
constexpr double kDrillDepth = 4.0;

constexpr const char* kStarter = R"(part block

param size = 30 mm

sketch base on XY {
  rect outline center (0 mm, 0 mm) size (size, size)
}

extrude body = base up 10 mm
)";

class StudioWindow : public QMainWindow {
public:
    explicit StudioWindow(QString path) : m_path(std::move(path))
    {
        auto* splitter = new QSplitter(this);
        auto* left = new QWidget(splitter);
        auto* column = new QVBoxLayout(left);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        m_editor = new QPlainTextEdit(left);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(11);
        m_editor->setFont(mono);
        m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_editor->setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(' ') * 2);
        m_editor->installEventFilter(this);
        m_problems = new QListWidget(left);
        m_problems->setMaximumHeight(190);
        m_problems->setWordWrap(true);
        column->addWidget(m_editor, 1);
        column->addWidget(m_problems);
        m_view = new studio::ViewWidget(splitter);
        splitter->addWidget(left);
        splitter->addWidget(m_view);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({520, 900});
        setCentralWidget(splitter);
        m_selector = new QLabel(this);
        m_selector->setTextInteractionFlags(Qt::TextSelectableByMouse);
        statusBar()->addWidget(m_selector, 1);
        m_timing = new QLabel(this);
        statusBar()->addPermanentWidget(m_timing);

        setStyleSheet(R"(
            QMainWindow, QWidget { background: #1d2025; color: #d9dde3; }
            QPlainTextEdit { background: #16181c; border: none; padding: 6px; selection-background-color: #3a4250; }
            QListWidget { background: #1a1d21; border: none; border-top: 1px solid #2c3138; }
            QListWidget::item { padding: 3px 6px; }
            QStatusBar { background: #16181c; color: #aeb4bd; }
            QSplitter::handle { background: #2c3138; width: 1px; }
        )");

        m_debounce.setSingleShot(true);
        m_debounce.setInterval(kDebounceMs);
        connect(&m_debounce, &QTimer::timeout, this, [this] { rebuild(); });
        connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
            m_debounce.start();
            forgetAction();
        });
        connect(m_problems, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) { jumpTo(item); });
        connect(m_problems, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { jumpTo(item); });

        m_view->hovered = [this](int face) { showFace(face, false); };
        m_view->clicked = [this](int face) {
            m_chosen = face;
            m_view->setSelected(face >= 0 ? std::set<int>{face} : std::set<int>{});
            showFace(face, true);
        };
        m_view->activated = [this](int face) { insertSelector(face); };

        auto* save = new QShortcut(QKeySequence::Save, this);
        connect(save, &QShortcut::activated, this, [this] { saveFile(); });
        auto* formatKey = new QShortcut(QKeySequence("Ctrl+Shift+F"), this);
        connect(formatKey, &QShortcut::activated, this, [this] { formatSource(); });
        auto* drillKey = new QShortcut(QKeySequence("Ctrl+E"), this);
        connect(drillKey, &QShortcut::activated, this, [this] { drillChosenFace(); });
        auto* holdKey = new QShortcut(QKeySequence("Ctrl+L"), this);
        connect(holdKey, &QShortcut::activated, this, [this] { holdChosenLength(); });
        auto* undoKey = new QShortcut(QKeySequence::Undo, m_view);
        connect(undoKey, &QShortcut::activated, m_editor, &QPlainTextEdit::undo);

        m_rebuilder = std::make_unique<studio::Rebuilder>(
            [this](int generation, const std::shared_ptr<const model::Snapshot>& snapshot) {
                QMetaObject::invokeMethod(
                    this, [this, generation, snapshot] { built(generation, snapshot); }, Qt::QueuedConnection);
            });

        QString text = kStarter;
        QFile file(m_path);
        if (!m_path.isEmpty() && file.open(QIODevice::ReadOnly)) {
            text = QString::fromUtf8(file.readAll());
            m_crlf = text.contains("\r\n");
            text.remove('\r');
        }
        m_editor->setPlainText(text);
        updateTitle();
        resize(1440, 860);
    }

    std::function<void()> afterFirstBuild;

    void selectFaces(const QString& selector) { m_pendingSelect = selector; }

    void typeAtEnd(const QString& text)
    {
        QTextCursor cursor = m_editor->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(text);
        m_editor->setTextCursor(cursor);
    }

    QString text() const { return m_editor->toPlainText(); }

    void undo() { m_editor->undo(); }

    void setParamLine(const QString& name, const QString& value)
    {
        const QString text = m_editor->toPlainText();
        const QRegularExpression pattern("^param " + QRegularExpression::escape(name) + " = .*$",
                                         QRegularExpression::MultilineOption);
        const QRegularExpressionMatch match = pattern.match(text);
        if (match.hasMatch()) {
            QTextCursor cursor(m_editor->document());
            cursor.setPosition(static_cast<int>(match.capturedStart()));
            cursor.setPosition(static_cast<int>(match.capturedEnd()), QTextCursor::KeepAnchor);
            cursor.insertText("param " + name + " = " + value);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_editor && event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if ((key->modifiers() & Qt::ControlModifier) && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
                double step = 1.0;
                if (key->modifiers() & Qt::ShiftModifier) {
                    step = 10.0;
                } else if (key->modifiers() & Qt::AltModifier) {
                    step = 0.1;
                }
                scrubNumber(key->key() == Qt::Key_Up ? step : -step);
                return true;
            }
        }
        return QMainWindow::eventFilter(watched, event);
    }

public:
    void drillChosenFace()
    {
        const model::Snapshot* snapshot = m_view->snapshot();
        if (snapshot == nullptr || m_chosen < 0 || m_chosen >= static_cast<int>(snapshot->faces.size())) {
            m_selector->setText("Click a flat face first, then press Ctrl+E to drill it");
            return;
        }
        if (m_stale) {
            m_selector->setText("The view is out of date. Fix the errors before drilling");
            return;
        }
        const model::Face& face = snapshot->faces[m_chosen];
        if (face.kind != "plane") {
            m_selector->setText("A hole needs a flat face, and " + QString::fromStdString(face.selector) +
                                " is curved");
            return;
        }
        if (face.selector.rfind("all(", 0) == 0) {
            m_selector->setText("This piece of " + QString::fromStdString(face.name) +
                                " has no single name, so a sketch cannot sit on it");
            return;
        }
        const model::Frame frame = model::planeFrame(face.normal, face.centroid);
        const double u = model::dot(face.centroid - frame.origin, frame.x);
        const double v = model::dot(face.centroid - frame.origin, frame.y);
        std::string text;
        try {
            text =
                lang::addHole(m_editor->toPlainText().toStdString(), face.selector, u, v, kDrillDiameter, kDrillDepth);
        } catch (const std::exception& error) {
            m_selector->setText(QString::fromStdString(error.what()));
            return;
        }
        applyAction(text, "drilled " + QString::fromStdString(face.selector));
    }

    void holdChosenLength()
    {
        const model::Snapshot* snapshot = m_view->snapshot();
        if (snapshot == nullptr || m_chosen < 0 || m_chosen >= static_cast<int>(snapshot->faces.size())) {
            m_selector->setText("Click a side made by a sketch line first, then press Ctrl+L to hold its length");
            return;
        }
        if (m_stale) {
            m_selector->setText("The view is out of date. Fix the errors before holding a length");
            return;
        }
        const model::Face& face = snapshot->faces[m_chosen];
        const QStringList parts = QString::fromStdString(face.name).split('.');
        const model::SolvedSketch* sketch = nullptr;
        const model::SolvedLine* line = nullptr;
        for (const model::SolvedSketch& candidate : snapshot->sketches) {
            for (const model::SolvedLine& l : candidate.lines) {
                if (parts.size() == 3 && candidate.name == parts[1].toStdString() && l.name == parts[2].toStdString()) {
                    sketch = &candidate;
                    line = &l;
                }
            }
        }
        if (line == nullptr) {
            m_selector->setText("Ctrl+L holds a side made by a sketch line, and " +
                                QString::fromStdString(face.selector) + " is not one");
            return;
        }
        const auto isFree = [sketch](const std::string& point) {
            return std::any_of(sketch->points.begin(), sketch->points.end(),
                               [&](const model::SolvedPoint& p) { return p.name == point && p.free; });
        };
        if (!isFree(line->from) && !isFree(line->to)) {
            m_selector->setText("The length of " + QString::fromStdString(line->name) +
                                " is already set by the rest of sketch " + QString::fromStdString(sketch->name));
            return;
        }
        const std::string constraint =
            "length(" + line->name + ") = " + lang::formatNumber(std::round(line->length * 1000.0) / 1000.0) + " mm";
        std::string text;
        try {
            text = lang::addConstraint(m_editor->toPlainText().toStdString(), sketch->name, constraint);
        } catch (const std::exception& error) {
            m_selector->setText(QString::fromStdString(error.what()));
            return;
        }
        applyAction(text, "held " + QString::fromStdString(constraint) + " in sketch " +
                              QString::fromStdString(sketch->name));
    }

private:
    void applyAction(const std::string& text, const QString& done)
    {
        m_beforeAction = m_editor->toPlainText();
        QTextCursor cursor(m_editor->document());
        cursor.beginEditBlock();
        cursor.select(QTextCursor::Document);
        cursor.insertText(QString::fromStdString(text));
        cursor.endEditBlock();
        m_afterAction = m_editor->toPlainText();
        m_action = done;
        m_selector->setText(done + "   (Ctrl+Z takes it back)");
    }

    void forgetAction()
    {
        if (m_action.isEmpty()) {
            return;
        }
        const QString text = m_editor->toPlainText();
        if (text == m_afterAction) {
            m_selector->setText(m_action + "   (Ctrl+Z takes it back)");
        } else if (text == m_beforeAction) {
            m_selector->setText("took back: " + m_action);
        } else {
            m_selector->setText(QString());
            m_action.clear();
        }
    }

    void rebuild()
    {
        m_requested = m_rebuilder->request(m_editor->toPlainText().toStdString());
        m_timing->setText("building...");
    }

    void built(int generation, const std::shared_ptr<const model::Snapshot>& snapshot)
    {
        if (generation != m_requested) {
            return;
        }
        m_latest = snapshot;
        const std::string kept = chosenSelector();
        const bool hardError =
            std::any_of(snapshot->diagnostics.begin(), snapshot->diagnostics.end(), [](const auto& d) {
                return d.severity == lang::Diagnostic::Severity::Error && d.code != "check";
            });
        m_stale = !(snapshot->built && !hardError);
        if (!m_stale) {
            m_good = snapshot;
            m_view->setSnapshot(snapshot, false);
        } else if (m_good) {
            m_view->setSnapshot(m_good, true);
        } else if (snapshot->built) {
            m_view->setSnapshot(snapshot, true);
        }
        m_chosen = -1;
        m_view->setSelected({});
        showProblems(*snapshot);
        markLines(*snapshot);
        m_timing->setText(QString("rebuild %1 ms").arg(snapshot->totalMilliseconds, 0, 'f', 1));
        if (!m_pendingSelect.isEmpty() && m_view->snapshot() != nullptr) {
            const lang::ExpressionResult parsed = lang::parseExpression(m_pendingSelect.toStdString());
            if (parsed.expr) {
                try {
                    const model::Selection selection = model::select(*parsed.expr, *m_view->snapshot());
                    if (!selection.edges && !selection.items.empty()) {
                        m_view->setSelected({selection.items.begin(), selection.items.end()});
                        m_chosen = selection.items.front();
                        showFace(selection.items.front(), true);
                    }
                } catch (const lang::EvalError& error) {
                    m_selector->setText(QString::fromStdString(error.what()));
                }
            }
            m_pendingSelect.clear();
        } else if (!kept.empty()) {
            choose(kept);
        }
        if (afterFirstBuild) {
            auto callback = std::move(afterFirstBuild);
            afterFirstBuild = nullptr;
            QTimer::singleShot(150, this, callback);
        }
    }

    std::string chosenSelector() const
    {
        const model::Snapshot* shown = m_view->snapshot();
        if (shown == nullptr || m_chosen < 0 || m_chosen >= static_cast<int>(shown->faces.size())) {
            return {};
        }
        return shown->faces[m_chosen].selector;
    }

    void choose(const std::string& selector)
    {
        const lang::ExpressionResult parsed = lang::parseExpression(selector);
        if (!parsed.expr || m_view->snapshot() == nullptr) {
            return;
        }
        try {
            const model::Selection selection = model::select(*parsed.expr, *m_view->snapshot());
            if (!selection.edges && selection.items.size() == 1) {
                m_view->setSelected({selection.items.front()});
                m_chosen = selection.items.front();
            }
        } catch (const lang::EvalError&) {
            m_chosen = -1;
            m_view->setSelected({});
        }
    }

    void showProblems(const model::Snapshot& snapshot)
    {
        m_problems->clear();
        for (const lang::Diagnostic& d : snapshot.diagnostics) {
            if (d.code == "check") {
                continue;
            }
            QString text = QString("line %1  %2").arg(d.span.line).arg(QString::fromStdString(d.message));
            if (!d.hint.empty()) {
                text += "\n    " + QString::fromStdString(d.hint);
            }
            auto* item = new QListWidgetItem(text, m_problems);
            item->setData(Qt::UserRole, d.span.line);
            item->setForeground(d.severity == lang::Diagnostic::Severity::Error ? QColor(240, 120, 110)
                                                                                : QColor(200, 190, 140));
        }
        for (const model::CheckResult& check : snapshot.checks) {
            const QString text = QString("%1  %2: %3 is %4, needs %5 %6")
                                     .arg(check.passed ? "pass" : "FAIL")
                                     .arg(QString::fromStdString(check.name))
                                     .arg(QString::fromStdString(check.expression))
                                     .arg(QString::fromStdString(lang::toText(check.lhs)))
                                     .arg(QString::fromStdString(check.comparison))
                                     .arg(QString::fromStdString(lang::toText(check.rhs)));
            auto* item = new QListWidgetItem(text, m_problems);
            item->setData(Qt::UserRole, check.span.line);
            item->setForeground(check.passed ? QColor(130, 200, 140) : QColor(240, 120, 110));
        }
    }

    void markLines(const model::Snapshot& snapshot)
    {
        QList<QTextEdit::ExtraSelection> marks;
        for (const lang::Diagnostic& d : snapshot.diagnostics) {
            if (d.severity != lang::Diagnostic::Severity::Error || d.span.line <= 0) {
                continue;
            }
            QTextEdit::ExtraSelection mark;
            mark.cursor = QTextCursor(m_editor->document()->findBlockByNumber(d.span.line - 1));
            mark.format.setBackground(QColor(90, 40, 40));
            mark.format.setProperty(QTextFormat::FullWidthSelection, true);
            marks.append(mark);
        }
        m_editor->setExtraSelections(marks);
    }

    void jumpTo(QListWidgetItem* item)
    {
        const int line = item->data(Qt::UserRole).toInt();
        if (line > 0) {
            QTextCursor cursor(m_editor->document()->findBlockByNumber(line - 1));
            m_editor->setTextCursor(cursor);
            m_editor->setFocus();
        }
    }

    void showFace(int face, bool chosen)
    {
        const model::Snapshot* snapshot = m_view->snapshot();
        if (snapshot == nullptr || face < 0 || face >= static_cast<int>(snapshot->faces.size())) {
            if (chosen) {
                m_selector->clear();
            }
            return;
        }
        const QString selector = QString::fromStdString(model::selectorFor(*snapshot, face));
        const QString words = QString::fromStdString(model::describeFace(*snapshot, face));
        m_selector->setText((chosen ? "selected " : "") + selector + "   |   " + words +
                            (chosen ? "   (double-click inserts it)" : ""));
    }

    void insertSelector(int face)
    {
        const model::Snapshot* snapshot = m_view->snapshot();
        if (snapshot == nullptr) {
            return;
        }
        m_editor->textCursor().insertText(QString::fromStdString(model::selectorFor(*snapshot, face)));
        m_editor->setFocus();
    }

    void scrubNumber(double step)
    {
        QTextCursor cursor = m_editor->textCursor();
        const QString line = cursor.block().text();
        const int at = cursor.positionInBlock();
        static const QRegularExpression kNumber("\\d+(\\.\\d+)?|\\.\\d+");
        auto it = kNumber.globalMatch(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            if (at < match.capturedStart() || at > match.capturedEnd()) {
                continue;
            }
            const double value = std::max(0.0, match.captured().toDouble() + step);
            const QString text = QString::fromStdString(lang::formatNumber(std::round(value * 1e6) / 1e6));
            QTextCursor edit(cursor.block());
            edit.setPosition(cursor.block().position() + static_cast<int>(match.capturedStart()));
            edit.setPosition(cursor.block().position() + static_cast<int>(match.capturedEnd()),
                             QTextCursor::KeepAnchor);
            edit.insertText(text);
            cursor.setPosition(cursor.block().position() + static_cast<int>(match.capturedStart()) +
                               std::min(static_cast<int>(at - match.capturedStart()), static_cast<int>(text.size())));
            m_editor->setTextCursor(cursor);
            return;
        }
    }

    void formatSource()
    {
        const lang::ParseResult parsed = lang::parse(m_editor->toPlainText().toStdString());
        if (lang::hasErrors(parsed.diagnostics)) {
            return;
        }
        const int line = m_editor->textCursor().blockNumber();
        QTextCursor all(m_editor->document());
        all.select(QTextCursor::Document);
        all.insertText(QString::fromStdString(lang::format(parsed.document)));
        m_editor->setTextCursor(QTextCursor(m_editor->document()->findBlockByNumber(line)));
    }

    void saveFile()
    {
        if (m_path.isEmpty()) {
            return;
        }
        QString text = m_editor->toPlainText();
        if (m_crlf) {
            text.replace('\n', "\r\n");
        }
        const QByteArray bytes = text.toUtf8();
        QFile file(m_path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()) {
            m_selector->setText("Could not save " + m_path + ": " + file.errorString());
            return;
        }
        m_selector->setText("saved " + QFileInfo(m_path).fileName());
        updateTitle();
    }

    void updateTitle()
    {
        setWindowTitle((m_path.isEmpty() ? QString("untitled") : QFileInfo(m_path).fileName()) + " - Kalve studio");
    }

    QString m_path;
    QPlainTextEdit* m_editor = nullptr;
    QListWidget* m_problems = nullptr;
    studio::ViewWidget* m_view = nullptr;
    QLabel* m_selector = nullptr;
    QLabel* m_timing = nullptr;
    QTimer m_debounce;
    int m_requested = 0;
    std::shared_ptr<const model::Snapshot> m_latest;
    std::shared_ptr<const model::Snapshot> m_good;
    QString m_pendingSelect;
    int m_chosen = -1;
    bool m_stale = false;
    bool m_crlf = false;
    QString m_action;
    QString m_beforeAction;
    QString m_afterAction;
    std::unique_ptr<studio::Rebuilder> m_rebuilder;
};

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    look::loadFonts(QApplication::applicationDirPath());
    QStringList args = app.arguments();
    args.removeFirst();
    QString path;
    QString shot;
    QString select;
    QString script;
    QString prefix;
    std::function<void(StudioWindow&)> action;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == "--shot" && i + 1 < args.size()) {
            shot = args[++i];
        } else if (args[i] == "--select" && i + 1 < args.size()) {
            select = args[++i];
        } else if (args[i] == "--edit" && i + 1 < args.size()) {
            script = args[++i];
        } else if (args[i] == "--drill" && i + 1 < args.size()) {
            prefix = args[++i];
            action = [](StudioWindow& w) { w.drillChosenFace(); };
        } else if (args[i] == "--hold" && i + 1 < args.size()) {
            prefix = args[++i];
            action = [](StudioWindow& w) { w.holdChosenLength(); };
        } else {
            path = args[i];
        }
    }
    StudioWindow window(path);
    window.show();
    if (!select.isEmpty()) {
        window.selectFaces(select);
    }
    if (action) {
        const auto keep = [&window](const QString& path) {
            window.grab().save(path + ".png");
            QFile file(path + ".kd");
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(window.text().toUtf8());
            }
        };
        window.afterFirstBuild = [&window, prefix, keep, action] {
            keep(prefix + "-before");
            action(window);
            QTimer::singleShot(1500, &window, [&window, prefix, keep] {
                keep(prefix + "-done");
                window.undo();
                QTimer::singleShot(1500, &window, [prefix, keep] {
                    keep(prefix + "-undone");
                    QApplication::quit();
                });
            });
        };
    }
    if (!shot.isEmpty()) {
        window.afterFirstBuild = [&window, shot, script] {
            if (!script.isEmpty()) {
                const QStringList parts = script.split('=');
                window.setParamLine(parts.value(0), parts.value(1));
                QTimer::singleShot(1500, &window, [&window, shot] {
                    window.grab().save(shot);
                    QApplication::quit();
                });
                return;
            }
            window.grab().save(shot);
            QApplication::quit();
        };
    }
    return QApplication::exec();
}
