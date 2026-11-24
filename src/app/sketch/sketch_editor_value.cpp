// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/feature/recent_values.h"
#include "app/feature/value_edit.h"
#include "app/session/design_session.h"
#include "app/sketch/sketch_editor_detail.h"
#include "app/theme/fonts.h"
#include "app/theme/theme.h"
#include "core/parameter_table.h"

#include "sketch/dimension.h"
#include "sketch/dimension_layout.h"
#include "viewer/occt_view_widget.h"

#include <QFontMetricsF>
#include <QLineEdit>
#include <QScopedValueRollback>
#include <QTimer>

#include <algorithm>
#include <optional>
#include <utility>
namespace kalve137::app {

using namespace sketch_editor_detail;

void SketchEditor::addDimension(sketch::Constraint constraint, QPointF pos)
{
    const ConstraintKind kind = constraint.kind;
    const double value = constraint.value;
    const Id label = current().nextId();
    const auto build = [this, constraint = std::move(constraint)](double typed, const std::string& expression) {
        sketch::Sketch candidate = current();
        const Id id = candidate.addConstraint(constraint.kind, constraint.refs, typed);
        candidate.constraint(id)->labelOffset = constraint.labelOffset;
        candidate.constraint(id)->expression = expression;
        return candidate;
    };
    promptValue(
        pos, value, quantityOf(kind), label,
        [this, kind, build](double typed, const std::string& expression) {
            if (typed <= 0.0 && kind != ConstraintKind::Angle) {
                emit message(tr("Dimensions must be greater than zero"));
                return;
            }
            commit(tr("Dimension"), build(typed, expression));
        },
        [build](double typed) { return build(typed, {}); },
        core::takesExpression(kind) ? std::optional(QString()) : std::nullopt);
}

void SketchEditor::editDimension(Id constraint, QPointF pos)
{
    const sketch::Constraint* c = current().constraint(constraint);
    if (c == nullptr || !sketch::isDimension(c->kind)) {
        return;
    }
    const ConstraintKind kind = c->kind;
    if (const std::optional<QPointF> label = labelOnScreen(constraint)) {
        pos = *label;
    }
    const auto build = [this, constraint](double typed, const std::string& expression) {
        sketch::Sketch candidate = current();
        if (sketch::Constraint* edited = candidate.constraint(constraint)) {
            edited->value = typed;
            edited->expression = expression;
        }
        return candidate;
    };
    const std::optional<QString> expression =
        core::takesExpression(kind) ? std::optional(QString::fromStdString(c->expression)) : std::nullopt;
    promptValue(
        pos, c->value, quantityOf(kind), constraint,
        [this, kind, build](double typed, const std::string& typedExpression) {
            if (typed <= 0.0 && kind != ConstraintKind::Angle) {
                emit message(tr("Dimensions must be greater than zero"));
                return;
            }
            sketch::Sketch candidate = build(typed, typedExpression);
            if (kind == ConstraintKind::Offset) {
                sketch::Sketch probe = candidate;
                if (!sketch::solve(probe).solved) {
                    emit message(tr("A %1 mm offset doesn't fit these curves").arg(formatValue(typed)));
                    return;
                }
            }
            commit(tr("Edit Dimension"), std::move(candidate));
        },
        [build](double typed) { return build(typed, {}); }, expression);
}

ParameterSource SketchEditor::valueNames() const
{
    if (!m_valueTakesNames) {
        return {};
    }
    return [this] { return core::parameterValues(m_session.document()); };
}

void SketchEditor::promptValue(QPointF pos, double initial, core::Quantity quantity, Id label,
                               std::function<void(double, const std::string&)> apply,
                               std::function<sketch::Sketch(double)> preview, std::optional<QString> expression)
{
    m_valueApply = std::move(apply);
    m_valuePreview = std::move(preview);
    m_valueQuantity = quantity;
    m_editingLabel = label;
    m_valueTakesNames = expression.has_value();
    m_valueError->dismiss();
    m_valueEditor->setText(expression && !expression->isEmpty() ? *expression : formatValue(initial));
    m_recentValues->reset();
    previewValue(m_valueEditor->text());
    const int trailing = recentValues(quantity).empty() ? 0 : kRecentValuesPixels;
    QPoint at;
    if (label != 0 && m_editedLabel) {
        QFont font(fonts::families().qt);
        font.setPixelSize(static_cast<int>(kDimensionTextPixels));
        m_valueEditor->setFont(font);
        const QFontMetricsF metrics(font);
        const double room = metrics.horizontalAdvance(QStringLiteral("0000.00")) + 2.0 * theme::kSpaceS + trailing;
        const QSize size(
            static_cast<int>(std::ceil(std::max(m_editedLabel->width(), room))),
            static_cast<int>(std::ceil(std::max(m_editedLabel->height(), double(m_valueEditor->sizeHint().height())))));
        m_valueEditor->setFixedSize(size);
        m_valueEditor->setAlignment(Qt::AlignCenter);
        at = (m_editedLabel->center() - QPointF(size.width(), size.height()) / 2.0).toPoint();
    } else {
        m_valueEditor->setFont(canvas()->font());
        m_valueEditor->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_valueEditor->setFixedSize(kValueEditorWidth + trailing, m_valueEditor->sizeHint().height());
        at = pos.toPoint() + QPoint(static_cast<int>(kInputGapPixels), -m_valueEditor->height() - theme::kSpaceXs);
    }
    m_valueEditor->move(std::clamp(at.x(), 0, std::max(0, canvas()->width() - m_valueEditor->width())),
                        std::clamp(at.y(), 0, std::max(0, canvas()->height() - m_valueEditor->height())));
    m_valueEditor->show();
    m_valueEditor->raise();
    m_valueEditor->setFocus();
    m_valueEditor->selectAll();
}

void SketchEditor::previewValue(const QString& text)
{
    if (!m_valuePreview) {
        return;
    }
    m_valueError->dismiss();
    std::optional<sketch::Sketch> shown;
    if (const TypedValue typed = readTyped(text, m_valueQuantity, valueNames()); !typed.error) {
        sketch::Sketch candidate = m_valuePreview(typed.value);
        const sketch::SolveResult result = sketch::solve(candidate);
        if (result.solved && result.conflicting.empty()) {
            shown = std::move(candidate);
        }
    }
    m_dragPreview = std::move(shown);
    m_previewingValue = true;
    redraw();
}

void SketchEditor::closeValueEditor()
{
    m_offsetChain.clear();
    m_valueApply = nullptr;
    m_valuePreview = nullptr;
    m_valueTakesNames = false;
    m_valueError->dismiss();
    m_valueNames->hide();
    const bool labelHidden = std::exchange(m_editingLabel, 0) != 0;
    if (std::exchange(m_previewingValue, false)) {
        m_dragPreview.reset();
        redraw();
    } else if (labelHidden) {
        redraw();
    }
    if (m_valueEditor->isVisible()) {
        m_valueEditor->hide();
        canvas()->setFocus();
    }
}

} // namespace kalve137::app
