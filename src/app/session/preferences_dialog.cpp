// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/preferences_dialog.h"

#include "app/theme/theme.h"
#include "app/view/grid_settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace kalve137::app {

namespace {

using viewer::NavigationPreset;

constexpr int kPageListWidth = 160;
constexpr QSize kDialogSize{theme::kWidthWide, 440};

template <typename Enum>
void select(QComboBox* combo, Enum value)
{
    combo->setCurrentIndex(combo->findData(static_cast<int>(value)));
}

template <typename Enum>
Enum selected(const QComboBox* combo)
{
    return static_cast<Enum>(combo->currentData().toInt());
}

} // namespace

PreferencesDialog::PreferencesDialog(const Preferences& preferences, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search preferences"));
    m_search->setClearButtonEnabled(true);
    m_pages = new QListWidget(this);
    m_pages->setFixedWidth(kPageListWidth);
    m_stack = new QStackedWidget(this);

    QFormLayout* general = addPage(tr("General"));
    m_reopenLastProject = addCheck(general, tr("Reopen the last project on start"), tr("startup recent"));

    QFormLayout* navigation = addPage(tr("Navigation"));
    m_preset = addCombo(navigation, tr("Mouse scheme"), tr("preset orbit pan zoom middle button"));
    for (const NavigationPreset preset :
         {NavigationPreset::MiddleOrbitsCtrlPans, NavigationPreset::MiddlePansShiftOrbits,
          NavigationPreset::MiddleOrbitsShiftPans}) {
        m_preset->addItem(displayName(preset), static_cast<int>(preset));
    }
    m_presetHint = new QLabel(this);
    m_presetHint->setObjectName("panelHint");
    m_presetHint->setWordWrap(true);
    navigation->addRow(QString(), m_presetHint);
    addRow(navigation, tr("Mouse scheme preset orbit pan zoom middle button"));
    connect(m_preset, &QComboBox::currentIndexChanged, this, &PreferencesDialog::updatePresetHint);
    m_reverseZoom = addCheck(navigation, tr("Reverse wheel zoom direction"), tr("scroll invert"));
    m_zoomToCursor = addCheck(navigation, tr("Zoom toward the cursor"), tr("wheel scroll point"));
    m_orbitAroundCursor =
        addCheck(navigation, tr("Orbit around the point under the cursor"), tr("rotate pivot center"));
    m_constrainedOrbit = addCheck(navigation, tr("Constrained orbit, keep Z up"), tr("rotate turntable"));
    m_animateTransitions = addCheck(navigation, tr("Animate view transitions"), tr("camera smooth motion"));

    QFormLayout* display = addPage(tr("Display"));
    m_visualStyle = addCombo(display, tr("Visual style"), tr("shaded wireframe edges hidden"));
    for (const VisualStyle style :
         {VisualStyle::Shaded, VisualStyle::ShadedWithEdges, VisualStyle::Wireframe, VisualStyle::VisibleEdges}) {
        m_visualStyle->addItem(displayName(style), static_cast<int>(style));
    }
    m_camera = addCombo(display, tr("Camera"), tr("projection orthographic perspective"));
    m_camera->addItem(tr("Orthographic"), 0);
    m_camera->addItem(tr("Perspective"), 1);
    m_showGrid = addCheck(display, tr("Show the grid"), tr("ground plane"));

    QFormLayout* sketch = addPage(tr("Sketch"));
    m_lookAtSketch = addCheck(sketch, tr("Look at the sketch plane when sketching starts"), tr("camera turn"));
    m_sketchProfiles = addCheck(sketch, tr("Shade profiles"), tr("palette regions closed loops"));
    m_sketchPoints = addCheck(sketch, tr("Show points"), tr("palette vertices ends"));
    m_sketchDimensions = addCheck(sketch, tr("Show dimensions"), tr("palette sizes values"));
    m_constraintGlyphs = addCheck(sketch, tr("Show constraint glyphs"), tr("palette badges icons horizontal vertical"));
    m_sketchConstruction = addCheck(sketch, tr("Show construction geometry"), tr("palette dashed reference"));
    m_sketchGridLabels = addCheck(sketch, tr("Label the grid while sketching"), tr("numbers rulers"));
    m_sketchGridSnap = addCheck(sketch, tr("Snap points to the grid"), tr("snapping intersections"));

    QFormLayout* grid = addPage(tr("Grid"));
    m_grid = new GridSettingsWidget(this);
    grid->addRow(m_grid);
    addRow(grid, tr("Grid spacing adaptive fixed incremental move step angle snap"));

    QFormLayout* diagnostics = addPage(tr("Diagnostics"));
    m_logLevels = new QLineEdit(this);
    m_logLevels->setPlaceholderText(QStringLiteral("info,viewer=debug"));
    m_logLevels->setToolTip(tr("A level for all channels, then channel=level pairs. Channels: app, core, geometry, "
                               "sketch, viewer, input, io. Levels: trace, debug, info, warn, error, off."));
    diagnostics->addRow(tr("Log levels"), m_logLevels);
    addRow(diagnostics, tr("Log levels logging debug trace channels"));

    connect(m_pages, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_pages->setCurrentRow(0);
    connect(m_search, &QLineEdit::textChanged, this, &PreferencesDialog::filter);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    auto* ok = new QPushButton(tr("OK"), buttons);
    ok->setObjectName("primaryButton");
    ok->setDefault(true);
    buttons->addButton(ok, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            [this] { load(Preferences{}); });

    auto* body = new QHBoxLayout;
    body->addWidget(m_pages);
    body->addWidget(m_stack, 1);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_search);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);

    load(preferences);
    resize(kDialogSize);
}

Preferences PreferencesDialog::preferences() const
{
    Preferences p = m_loaded;
    p.reopenLastProject = m_reopenLastProject->isChecked();
    p.navigation.preset = selected<NavigationPreset>(m_preset);
    p.navigation.reverseZoom = m_reverseZoom->isChecked();
    p.navigation.zoomToCursor = m_zoomToCursor->isChecked();
    p.navigation.orbitAroundCursor = m_orbitAroundCursor->isChecked();
    p.navigation.constrainedOrbit = m_constrainedOrbit->isChecked();
    p.navigation.animateTransitions = m_animateTransitions->isChecked();
    p.visualStyle = selected<VisualStyle>(m_visualStyle);
    p.perspective = m_camera->currentData().toInt() == 1;
    p.showGrid = m_showGrid->isChecked();
    p.lookAtSketch = m_lookAtSketch->isChecked();
    p.sketchProfiles = m_sketchProfiles->isChecked();
    p.sketchPoints = m_sketchPoints->isChecked();
    p.sketchDimensions = m_sketchDimensions->isChecked();
    p.constraintGlyphs = m_constraintGlyphs->isChecked();
    p.sketchConstruction = m_sketchConstruction->isChecked();
    p.sketchGridLabels = m_sketchGridLabels->isChecked();
    p.sketchGridSnap = m_sketchGridSnap->isChecked();
    m_grid->apply(p);
    p.logLevels = m_logLevels->text().trimmed();
    return p;
}

void PreferencesDialog::load(const Preferences& p)
{
    m_loaded = p;
    m_reopenLastProject->setChecked(p.reopenLastProject);
    select(m_preset, p.navigation.preset);
    m_reverseZoom->setChecked(p.navigation.reverseZoom);
    m_zoomToCursor->setChecked(p.navigation.zoomToCursor);
    m_orbitAroundCursor->setChecked(p.navigation.orbitAroundCursor);
    m_constrainedOrbit->setChecked(p.navigation.constrainedOrbit);
    m_animateTransitions->setChecked(p.navigation.animateTransitions);
    select(m_visualStyle, p.visualStyle);
    m_camera->setCurrentIndex(m_camera->findData(p.perspective ? 1 : 0));
    m_showGrid->setChecked(p.showGrid);
    m_lookAtSketch->setChecked(p.lookAtSketch);
    m_sketchProfiles->setChecked(p.sketchProfiles);
    m_sketchPoints->setChecked(p.sketchPoints);
    m_sketchDimensions->setChecked(p.sketchDimensions);
    m_constraintGlyphs->setChecked(p.constraintGlyphs);
    m_sketchConstruction->setChecked(p.sketchConstruction);
    m_sketchGridLabels->setChecked(p.sketchGridLabels);
    m_sketchGridSnap->setChecked(p.sketchGridSnap);
    m_grid->load(p);
    m_logLevels->setText(p.logLevels);
    updatePresetHint();
}

void PreferencesDialog::updatePresetHint()
{
    m_presetHint->setText(navigationHint(selected<NavigationPreset>(m_preset)).replace("   ", "\n"));
}

QFormLayout* PreferencesDialog::addPage(const QString& title)
{
    auto* page = new QWidget(m_stack);
    auto* heading = new QLabel(title, page);
    heading->setObjectName("panelTitle");
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(heading);
    layout->addLayout(form);
    layout->addStretch(1);
    m_stack->addWidget(page);
    m_pages->addItem(title);
    return form;
}

QCheckBox* PreferencesDialog::addCheck(QFormLayout* form, const QString& text, const QString& keywords)
{
    auto* check = new QCheckBox(text, this);
    form->addRow(check);
    addRow(form, text + ' ' + keywords);
    return check;
}

QComboBox* PreferencesDialog::addCombo(QFormLayout* form, const QString& label, const QString& keywords)
{
    auto* combo = new QComboBox(this);
    form->addRow(label, combo);
    addRow(form, label + ' ' + keywords);
    return combo;
}

void PreferencesDialog::addRow(QFormLayout* form, QString text)
{
    const int page = m_stack->count() - 1;
    text += ' ' + m_pages->item(page)->text();
    m_rows.push_back({page, form, form->rowCount() - 1, std::move(text)});
}

void PreferencesDialog::filter(const QString& text)
{
    const QStringList words = text.split(' ', Qt::SkipEmptyParts);
    std::vector<int> matches(static_cast<std::size_t>(m_stack->count()), 0);
    for (const Row& row : m_rows) {
        const bool match = std::ranges::all_of(
            words, [&row](const QString& word) { return row.text.contains(word, Qt::CaseInsensitive); });
        row.form->setRowVisible(row.index, match);
        matches[static_cast<std::size_t>(row.page)] += match ? 1 : 0;
    }
    int firstVisible = -1;
    for (int i = 0; i < m_pages->count(); ++i) {
        const bool visible = matches[static_cast<std::size_t>(i)] > 0;
        m_pages->item(i)->setHidden(!visible);
        if (visible && firstVisible < 0) {
            firstVisible = i;
        }
    }
    const QListWidgetItem* current = m_pages->currentItem();
    if (firstVisible >= 0 && (current == nullptr || current->isHidden())) {
        m_pages->setCurrentRow(firstVisible);
    }
}

} // namespace kalve137::app
