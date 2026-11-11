// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/feature_panel.h"

#include "app/feature/feature_panel_detail.h"
#include "app/feature/value_edit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <variant>

namespace kalve137::app {

using feature_panel_detail::kMaxLength;
using feature_panel_detail::makeLengthBox;

ValueEdit* feature_panel_detail::makeLengthBox(double value, double minimum, QWidget* parent)
{
    auto* box = new ValueEdit(core::Quantity::Length, value, parent);
    box->setRange(minimum, kMaxLength);
    return box;
}

ValueEdit* feature_panel_detail::makeAngleBox(double value, double minimum, double maximum, QWidget* parent)
{
    auto* box = new ValueEdit(core::Quantity::Angle, value, parent);
    box->setRange(minimum, maximum);
    box->setDecimals(2);
    box->setStep(15.0);
    return box;
}

namespace {

constexpr double kMaxTaper = 89.0;

QComboBox* makeOperationBox(core::Operation op, QWidget* parent)
{
    auto* box = new QComboBox(parent);
    box->addItems({FeaturePanel::tr("New Body"), FeaturePanel::tr("Join"), FeaturePanel::tr("Cut"),
                   FeaturePanel::tr("Intersect")});
    box->setCurrentIndex(static_cast<int>(op));
    return box;
}

} // namespace

using feature_panel_detail::titleOf;

void FeaturePanel::buildForm()
{
    delete m_form;
    m_form = new QWidget(this);
    m_layout->insertWidget(0, m_form);
    auto* layout = new QVBoxLayout(m_form);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* title = new QLabel(m_editing != 0 ? tr("Edit %1").arg(titleOf(*m_data)) : titleOf(*m_data), m_form);
    title->setObjectName("panelTitle");
    layout->addWidget(title);

    m_selection = new QLabel(m_form);
    m_selection->setWordWrap(true);
    layout->addWidget(m_selection);

    m_items = new QWidget(m_form);
    m_itemsLayout = new QVBoxLayout(m_items);
    m_itemsLayout->setContentsMargins(0, 0, 0, 0);
    m_itemsLayout->setSpacing(0);
    m_items->hide();
    layout->addWidget(m_items);

    auto* fields = new QFormLayout();
    layout->addLayout(fields);
    const auto changed = [this] { updatePreview(); };
    const auto addTangentChain = [&] {
        auto* chain = new QCheckBox(tr("Tangent chain"), m_form);
        chain->setChecked(m_tangentChain);
        chain->setToolTip(tr("Clicking an edge also picks the edges that run smoothly on from it"));
        fields->addRow(QString(), chain);
        connect(chain, &QCheckBox::toggled, this, [this](bool on) { m_tangentChain = on; });
    };

    std::visit(
        core::Overloaded{
            [](core::SketchFeature&) {},
            [&](core::ExtrudeFeature& f) {
                auto* distance = makeLengthBox(std::abs(f.distance), 0.001, m_form);
                auto* extent = new QComboBox(m_form);
                extent->addItems({tr("One side"), tr("Symmetric"), tr("Two sides")});
                extent->setCurrentIndex(static_cast<int>(f.extent));
                auto* second = makeLengthBox(f.secondDistance, 0.001, m_form);
                auto* flip = new QPushButton(tr("Flip"), m_form);
                flip->setCheckable(true);
                flip->setChecked(f.distance < 0.0);
                auto* operation = makeOperationBox(f.operation, m_form);
                auto* end = new QComboBox(m_form);
                end->addItems({tr("Distance"), tr("To object"), tr("All")});
                end->setCurrentIndex(static_cast<int>(f.end));
                m_target = new QPushButton(extrudeTargetName(f), m_form);
                m_target->setToolTip(tr("Click, then pick the flat face or construction plane to stop at"));
                auto* taper = feature_panel_detail::makeAngleBox(f.taperDegrees, -kMaxTaper, kMaxTaper, m_form);
                taper->setStep(1.0);
                m_distance = distance;
                m_secondDistance = second;
                m_flip = flip;
                fields->addRow(tr("Extent"), extent);
                fields->addRow(tr("End"), end);
                fields->addRow(tr("Distance"), distance);
                fields->addRow(tr("Second side"), second);
                fields->addRow(tr("Up to"), m_target);
                fields->addRow(tr("Direction"), flip);
                fields->addRow(tr("Taper"), taper);
                fields->addRow(tr("Operation"), operation);
                const auto showRows = [fields, extent, distance, second, flip](const core::ExtrudeFeature& shown,
                                                                               QPushButton* target) {
                    const bool toObject = shown.end == core::ExtrudeEnd::ToObject;
                    const bool byDistance = shown.end == core::ExtrudeEnd::Distance;
                    fields->setRowVisible(extent, !toObject);
                    fields->setRowVisible(distance, byDistance);
                    fields->setRowVisible(second, byDistance && shown.extent == core::ExtrudeExtent::TwoSides);
                    fields->setRowVisible(target, toObject);
                    fields->setRowVisible(flip, !toObject);
                };
                showRows(f, m_target);
                const auto apply = [this, distance, extent, end, second, flip, taper, operation, showRows, changed] {
                    auto& data = std::get<core::ExtrudeFeature>(*m_data);
                    data.distance = distance->value() * (flip->isChecked() ? -1.0 : 1.0);
                    data.extent = static_cast<core::ExtrudeExtent>(extent->currentIndex());
                    data.secondDistance = second->value();
                    data.operation = static_cast<core::Operation>(operation->currentIndex());
                    data.taperDegrees = taper->value();
                    const auto chosen = static_cast<core::ExtrudeEnd>(end->currentIndex());
                    if (chosen != data.end) {
                        data.end = chosen;
                        setPickingTarget(chosen == core::ExtrudeEnd::ToObject && !data.toFace && data.toPlane == 0);
                    }
                    showRows(data, m_target);
                    changed();
                };
                connect(distance, &ValueEdit::valueChanged, this, apply);
                connect(distance, &ValueEdit::accepted, this, &FeaturePanel::accept);
                connect(second, &ValueEdit::valueChanged, this, apply);
                connect(second, &ValueEdit::accepted, this, &FeaturePanel::accept);
                connect(taper, &ValueEdit::valueChanged, this, apply);
                connect(taper, &ValueEdit::accepted, this, &FeaturePanel::accept);
                connect(extent, &QComboBox::currentIndexChanged, this, apply);
                connect(end, &QComboBox::currentIndexChanged, this, apply);
                connect(m_target, &QPushButton::clicked, this, [this] {
                    setPickingTarget(true);
                    updatePreview();
                });
                connect(flip, &QPushButton::toggled, this, [operation](bool flipped) {
                    const auto join = static_cast<int>(core::Operation::Join);
                    const auto cut = static_cast<int>(core::Operation::Cut);
                    if (flipped && operation->currentIndex() == join) {
                        operation->setCurrentIndex(cut);
                    } else if (!flipped && operation->currentIndex() == cut) {
                        operation->setCurrentIndex(join);
                    }
                });
                connect(flip, &QPushButton::toggled, this, apply);
                connect(operation, &QComboBox::currentIndexChanged, this, apply);
            },
            [&](core::RevolveFeature& f) {
                m_axis = new QComboBox(m_form);
                m_axis->addItems(
                    {tr("Sketch line"), tr("X axis"), tr("Y axis"), tr("Z axis"), tr("Construction axis")});
                m_axis->setCurrentIndex(static_cast<int>(f.axis.kind));
                auto* angle = new ValueEdit(core::Quantity::Angle, f.angleDegrees, m_form);
                angle->setRange(0.1, 360.0);
                angle->setDecimals(2);
                angle->setStep(15.0);
                auto* operation = makeOperationBox(f.operation, m_form);
                fields->addRow(tr("Axis"), m_axis);
                fields->addRow(tr("Angle"), angle);
                fields->addRow(tr("Operation"), operation);
                connect(m_axis, &QComboBox::currentIndexChanged, this, [this, changed](int index) {
                    auto& data = std::get<core::RevolveFeature>(*m_data);
                    const auto kind = static_cast<core::AxisKind>(index);
                    if (kind == core::AxisKind::SketchLine || kind == core::AxisKind::Construction) {
                        if (data.axis.kind != kind) {
                            emit message(kind == core::AxisKind::SketchLine
                                             ? tr("Click a sketch line to use as the axis")
                                             : tr("Click a construction axis in the view"));
                        }
                        return;
                    }
                    data.axis = {kind, 0, 0};
                    changed();
                });
                connect(angle, &ValueEdit::valueChanged, this, [this, changed](double value) {
                    std::get<core::RevolveFeature>(*m_data).angleDegrees = value;
                    changed();
                });
                connect(angle, &ValueEdit::accepted, this, &FeaturePanel::accept);
                connect(operation, &QComboBox::currentIndexChanged, this, [this, changed](int index) {
                    std::get<core::RevolveFeature>(*m_data).operation = static_cast<core::Operation>(index);
                    changed();
                });
            },
            [&](core::FilletFeature& f) {
                const core::FilletSet& active = activeSet(f);
                auto* set = new QComboBox(m_form);
                for (std::size_t i = 0; i < f.sets.size(); ++i) {
                    set->addItem(tr("Set %1").arg(i + 1));
                }
                set->setCurrentIndex(static_cast<int>(m_filletSet));
                auto* add = new QPushButton(tr("Add set"), m_form);
                add->setToolTip(tr("Starts a new set of edges with its own radius"));
                m_size = makeLengthBox(active.radius, 0.001, m_form);
                auto* clear = new QPushButton(tr("Clear edges"), m_form);
                fields->addRow(tr("Set"), set);
                fields->setRowVisible(set, f.sets.size() > 1);
                fields->addRow(tr("Radius"), m_size);
                addTangentChain();
                fields->addRow(QString(), clear);
                fields->addRow(QString(), add);
                const auto rebuild = [this] {
                    QMetaObject::invokeMethod(
                        this,
                        [this] {
                            buildForm();
                            updatePreview();
                        },
                        Qt::QueuedConnection);
                };
                connect(set, &QComboBox::currentIndexChanged, this, [this, rebuild](int index) {
                    m_filletSet = static_cast<std::size_t>(std::max(index, 0));
                    rebuild();
                });
                connect(add, &QPushButton::clicked, this, [this, rebuild] {
                    auto& data = std::get<core::FilletFeature>(*m_data);
                    data.sets.push_back({{}, activeSet(data).radius});
                    m_filletSet = data.sets.size() - 1;
                    rebuild();
                });
                connect(m_size, &ValueEdit::valueChanged, this, [this, changed](double value) {
                    activeSet(std::get<core::FilletFeature>(*m_data)).radius = value;
                    changed();
                });
                connect(m_size, &ValueEdit::accepted, this, &FeaturePanel::accept);
                connect(clear, &QPushButton::clicked, this, [this, changed] {
                    activeSet(std::get<core::FilletFeature>(*m_data)).edges.clear();
                    changed();
                });
            },
            [&](core::ChamferFeature& f) {
                m_size = makeLengthBox(f.distance, 0.001, m_form);
                auto* clear = new QPushButton(tr("Clear edges"), m_form);
                fields->addRow(tr("Distance"), m_size);
                addTangentChain();
                fields->addRow(QString(), clear);
                connect(m_size, &ValueEdit::valueChanged, this, [this, changed](double value) {
                    std::get<core::ChamferFeature>(*m_data).distance = value;
                    changed();
                });
                connect(m_size, &ValueEdit::accepted, this, &FeaturePanel::accept);
                connect(clear, &QPushButton::clicked, this, [this, changed] {
                    std::get<core::ChamferFeature>(*m_data).edges.clear();
                    changed();
                });
            },
            [&](core::ShellFeature& f) {
                m_size = makeLengthBox(f.thickness, 0.001, m_form);
                auto* direction = new QComboBox(m_form);
                direction->addItems({tr("Inside"), tr("Outside")});
                direction->setCurrentIndex(f.inside ? 0 : 1);
                auto* clear = new QPushButton(tr("Clear faces"), m_form);
                fields->addRow(tr("Thickness"), m_size);
                fields->addRow(tr("Direction"), direction);
                fields->addRow(QString(), clear);
                connect(m_size, &ValueEdit::valueChanged, this, [this, changed](double value) {
                    std::get<core::ShellFeature>(*m_data).thickness = value;
                    changed();
                });
                connect(m_size, &ValueEdit::accepted, this, &FeaturePanel::accept);
                connect(direction, &QComboBox::currentIndexChanged, this, [this, changed](int index) {
                    std::get<core::ShellFeature>(*m_data).inside = index == 0;
                    changed();
                });
                connect(clear, &QPushButton::clicked, this, [this, changed] {
                    std::get<core::ShellFeature>(*m_data).faces.clear();
                    changed();
                });
            },
            [&](core::PatternFeature& f) {
                auto* kind = new QComboBox(m_form);
                kind->addItems({tr("Linear"), tr("Circular")});
                kind->setCurrentIndex(static_cast<int>(f.kind));
                auto* axis = new QComboBox(m_form);
                m_axis = axis;
                axis->addItems({tr("X axis"), tr("Y axis"), tr("Z axis"), tr("Construction axis")});
                axis->setCurrentIndex(static_cast<int>(f.axis.kind) - 1);
                auto* count = new QSpinBox(m_form);
                count->setRange(core::kMinPatternCount, core::kMaxPatternCount);
                count->setValue(f.count);
                auto* spacing = makeLengthBox(f.spacing, -kMaxLength, m_form);
                auto* angle = new ValueEdit(core::Quantity::Angle, f.angleDegrees, m_form);
                angle->setRange(0.1, 360.0);
                angle->setDecimals(2);
                angle->setStep(15.0);
                fields->addRow(tr("Type"), kind);
                fields->addRow(tr("Axis"), axis);
                fields->addRow(tr("Count"), count);
                fields->addRow(tr("Spacing"), spacing);
                fields->addRow(tr("Angle"), angle);
                const auto showRows = [fields, spacing, angle](core::PatternKind patternKind) {
                    fields->setRowVisible(spacing, patternKind == core::PatternKind::Linear);
                    fields->setRowVisible(angle, patternKind == core::PatternKind::Circular);
                };
                showRows(f.kind);
                connect(kind, &QComboBox::currentIndexChanged, this, [this, changed, showRows](int index) {
                    auto& data = std::get<core::PatternFeature>(*m_data);
                    data.kind = static_cast<core::PatternKind>(index);
                    showRows(data.kind);
                    changed();
                });
                connect(axis, &QComboBox::currentIndexChanged, this, [this, changed](int index) {
                    auto& data = std::get<core::PatternFeature>(*m_data);
                    const auto kind = static_cast<core::AxisKind>(index + 1);
                    if (kind == core::AxisKind::Construction) {
                        if (data.axis.kind != kind) {
                            emit message(tr("Click a construction axis in the view"));
                        }
                        return;
                    }
                    data.axis = {kind, 0, 0};
                    changed();
                });
                connect(count, &QSpinBox::valueChanged, this, [this, changed](int value) {
                    std::get<core::PatternFeature>(*m_data).count = value;
                    changed();
                });
                connect(spacing, &ValueEdit::valueChanged, this, [this, changed](double value) {
                    std::get<core::PatternFeature>(*m_data).spacing = value;
                    changed();
                });
                connect(spacing, &ValueEdit::accepted, this, &FeaturePanel::accept);
                connect(angle, &ValueEdit::valueChanged, this, [this, changed](double value) {
                    std::get<core::PatternFeature>(*m_data).angleDegrees = value;
                    changed();
                });
                connect(angle, &ValueEdit::accepted, this, &FeaturePanel::accept);
            },
            [](core::ImportFeature&) {},
            [&](core::PlaneFeature& f) { buildPlaneForm(f, fields); },
            [&](core::OffsetFaceFeature& f) { buildOffsetFaceForm(f, fields); },
            [&](core::HoleFeature& f) { buildHoleForm(f, fields); },
            [&](core::MoveFeature& f) { buildMoveForm(f, fields); },
            [](core::AxisFeature&) {},
            [](core::PointFeature&) {},
            [&](core::AppearanceFeature& f) { buildAppearanceForm(f, fields); },
        },
        *m_data);

    m_error = new QLabel(m_form);
    m_error->setObjectName("panelError");
    m_error->setWordWrap(true);
    layout->addWidget(m_error);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, m_form);
    m_ok = new QPushButton(tr("OK"), buttons);
    m_ok->setObjectName("primaryButton");
    buttons->addButton(m_ok, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &FeaturePanel::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &FeaturePanel::cancel);
    layout->addWidget(buttons);
}

} // namespace kalve137::app
