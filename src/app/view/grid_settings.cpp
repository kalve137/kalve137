// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/grid_settings.h"

#include "app/feature/value_edit.h"
#include "app/theme/theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace kalve137::app {

namespace {

constexpr double kMinSpacing = 0.01;
constexpr double kMaxSpacing = 10000.0;
constexpr double kMaxAngleIncrement = 90.0;
constexpr int kDialogWidth = theme::kWidthNarrow;

ValueEdit* makeBox(core::Quantity quantity, double maximum, QWidget* parent)
{
    auto* box = new ValueEdit(quantity, 1.0, parent);
    box->setRange(kMinSpacing, maximum);
    return box;
}

} // namespace

GridSettingsWidget::GridSettingsWidget(QWidget* parent) : QWidget(parent)
{
    m_adaptive = new QRadioButton(tr("Adaptive, follows zoom"), this);
    m_fixed = new QRadioButton(tr("Fixed spacing"), this);
    auto* group = new QButtonGroup(this);
    group->addButton(m_adaptive);
    group->addButton(m_fixed);
    m_spacing = makeBox(core::Quantity::Length, kMaxSpacing, this);
    m_spacing->setToolTip(tr("Lines grow 5 times apart when zoomed out too far to tell them apart"));
    m_incremental = new QCheckBox(tr("Incremental move"), this);
    m_incremental->setToolTip(tr("Dragged sketch points and arrows move in fixed steps"));
    m_moveIncrement = makeBox(core::Quantity::Length, kMaxSpacing, this);
    m_angleIncrement = makeBox(core::Quantity::Angle, kMaxAngleIncrement, this);

    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->addRow(m_adaptive);
    form->addRow(m_fixed);
    form->addRow(tr("Spacing"), m_spacing);
    form->addRow(m_incremental);
    form->addRow(tr("Move step"), m_moveIncrement);
    form->addRow(tr("Angle step"), m_angleIncrement);

    connect(m_fixed, &QRadioButton::toggled, this, &GridSettingsWidget::updateEnabled);
    connect(m_incremental, &QCheckBox::toggled, this, &GridSettingsWidget::updateEnabled);
}

void GridSettingsWidget::load(const Preferences& preferences)
{
    (preferences.gridAdaptive ? m_adaptive : m_fixed)->setChecked(true);
    m_spacing->setValue(preferences.gridSpacing);
    m_incremental->setChecked(preferences.incrementalMove);
    m_moveIncrement->setValue(preferences.moveIncrement);
    m_angleIncrement->setValue(preferences.angleIncrement);
    updateEnabled();
}

void GridSettingsWidget::apply(Preferences& preferences) const
{
    preferences.gridAdaptive = m_adaptive->isChecked();
    preferences.gridSpacing = m_spacing->value();
    preferences.incrementalMove = m_incremental->isChecked();
    preferences.moveIncrement = m_moveIncrement->value();
    preferences.angleIncrement = m_angleIncrement->value();
}

void GridSettingsWidget::updateEnabled()
{
    m_spacing->setEnabled(m_fixed->isChecked());
    m_moveIncrement->setEnabled(m_incremental->isChecked());
    m_angleIncrement->setEnabled(m_incremental->isChecked());
}

GridSettingsDialog::GridSettingsDialog(const Preferences& preferences, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                          Qt::WindowCloseButtonHint),
      m_preferences(preferences)
{
    setWindowTitle(tr("Grid Settings"));
    m_settings = new GridSettingsWidget(this);
    m_settings->load(preferences);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* ok = new QPushButton(tr("OK"), buttons);
    ok->setObjectName("primaryButton");
    ok->setDefault(true);
    buttons->addButton(ok, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_settings);
    layout->addWidget(buttons);
    setMinimumWidth(kDialogWidth);
}

Preferences GridSettingsDialog::preferences() const
{
    Preferences p = m_preferences;
    m_settings->apply(p);
    return p;
}

} // namespace kalve137::app
