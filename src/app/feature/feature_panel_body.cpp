// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/feature_panel.h"

#include "app/feature/feature_panel_detail.h"
#include "app/feature/value_edit.h"
#include "app/theme/theme.h"

#include "core/naming.h"
#include "geometry/modeling.h"
#include "geometry/shapes.h"
#include "geometry/topology.h"
#include "log/log.h"
#include "sketch/profiles.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QPushButton>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <variant>

namespace kalve137::app {

using feature_panel_detail::kMatchTolerance;
using feature_panel_detail::kMaxLength;
using feature_panel_detail::makeAngleBox;
using feature_panel_detail::makeLengthBox;

namespace {

constexpr double kMinSize = 0.001;
constexpr double kMinConeAngle = 1.0;
constexpr double kMaxConeAngle = 179.0;

constexpr int kSwatchSize = 22;

struct Preset {
    QColor color;
    const char* name;
};

const Preset kPresets[] = {
    {QColor(0x8A, 0x90, 0x99), QT_TRANSLATE_NOOP("FeaturePanel", "Titanium")},
    {QColor(0x3F, 0x3F, 0x3F), QT_TRANSLATE_NOOP("FeaturePanel", "Graphite")},
    {QColor(0xE8, 0xE8, 0xE8), QT_TRANSLATE_NOOP("FeaturePanel", "White")},
    {QColor(0x3B, 0x61, 0xB4), QT_TRANSLATE_NOOP("FeaturePanel", "Blue")},
    {QColor(0x1F, 0x8A, 0x4C), QT_TRANSLATE_NOOP("FeaturePanel", "Green")},
    {QColor(0xC0, 0x39, 0x2B), QT_TRANSLATE_NOOP("FeaturePanel", "Red")},
    {QColor(0xF0, 0xDF, 0xAD), QT_TRANSLATE_NOOP("FeaturePanel", "Brass")},
    {QColor(0xB8, 0x73, 0x33), QT_TRANSLATE_NOOP("FeaturePanel", "Copper")},
};

QColor toQColor(const geometry::Color& color)
{
    return QColor::fromRgbF(static_cast<float>(color.red), static_cast<float>(color.green),
                            static_cast<float>(color.blue));
}

geometry::Color toColor(const QColor& color)
{
    return {color.redF(), color.greenF(), color.blueF()};
}

void paintSwatch(QPushButton* button, const std::optional<geometry::Color>& color)
{
    button->setStyleSheet(color ? QStringLiteral("background-color: %1").arg(toQColor(*color).name()) : QString());
}

} // namespace

bool FeaturePanel::handOffPressPull(const Pick& pick)
{
    switch (pick.kind) {
    case Pick::Kind::Profile: {
        core::ExtrudeFeature extrude;
        extrude.operation = m_baseModel.bodies.empty() ? core::Operation::NewBody : core::Operation::Join;
        toggleProfile(extrude.profiles, pick);
        if (!extrude.profiles.seeds.empty()) {
            K137_DEBUG(Input, "Press Pull on a profile extrudes");
            m_pressPull = false;
            start(std::move(extrude), 0);
        }
        return true;
    }
    case Pick::Kind::Edge: {
        core::FilletFeature fillet;
        toggleEdge(fillet.sets.front().edges, pick);
        if (!fillet.sets.front().edges.empty()) {
            K137_DEBUG(Input, "Press Pull on an edge fillets");
            m_pressPull = false;
            start(std::move(fillet), 0);
        }
        return true;
    }
    case Pick::Kind::Face:
        m_pressPull = false;
        m_scene.setPickMode(PickMode::Faces);
        return false;
    case Pick::Kind::OriginPlane:
    case Pick::Kind::Vertex:
    case Pick::Kind::SketchLine:
    case Pick::Kind::SketchPoint:
    case Pick::Kind::ConstructionPlane:
    case Pick::Kind::OriginAxis:
    case Pick::Kind::ConstructionAxis:
    case Pick::Kind::ConstructionPoint:
        return true;
    }
    return true;
}

void FeaturePanel::toggleHolePosition(core::HoleFeature& hole, const Pick& pick)
{
    switch (pick.kind) {
    case Pick::Kind::SketchPoint: {
        if (hole.sketch != pick.feature) {
            hole.sketch = pick.feature;
            hole.sketchPoints.clear();
            hole.facePoints.clear();
        }
        const auto id = static_cast<sketch::Id>(pick.index);
        if (auto existing = std::ranges::find(hole.sketchPoints, id); existing != hole.sketchPoints.end()) {
            hole.sketchPoints.erase(existing);
        } else {
            hole.sketchPoints.push_back(id);
        }
        return;
    }
    case Pick::Kind::Face: {
        if (!pick.point) {
            return;
        }
        std::optional<core::TopoRef> ref;
        for (const core::Body& body : m_baseModel.bodies) {
            if (const auto face = geometry::findFace(body.shape, *pick.point, kMatchTolerance)) {
                ref = core::faceRef(body, *face, *pick.point);
                break;
            }
        }
        auto existing = std::ranges::find_if(hole.facePoints, [&](const core::TopoRef& placed) {
            return placed.point.Distance(*pick.point) <= hole.diameter;
        });
        if (hole.sketch == 0 && existing != hole.facePoints.end()) {
            hole.facePoints.erase(existing);
            return;
        }
        if (!ref) {
            emit message(tr("Click a face of the original body"));
            return;
        }
        if (hole.sketch != 0) {
            hole.sketch = 0;
            hole.sketchPoints.clear();
        }
        hole.facePoints.push_back(std::move(*ref));
        return;
    }
    case Pick::Kind::OriginPlane:
    case Pick::Kind::Edge:
    case Pick::Kind::Vertex:
    case Pick::Kind::Profile:
    case Pick::Kind::SketchLine:
    case Pick::Kind::ConstructionPlane:
    case Pick::Kind::OriginAxis:
    case Pick::Kind::ConstructionAxis:
    case Pick::Kind::ConstructionPoint:
        return;
    }
}

void FeaturePanel::toggleBody(core::MoveFeature& move, const Pick& pick)
{
    if (pick.kind != Pick::Kind::Face) {
        return;
    }
    const core::BodyKey key = pick.body();
    if (m_baseModel.body(key) == nullptr) {
        emit message(tr("Pick a body that exists before this move, not a copy it makes"));
        return;
    }
    if (auto existing = std::ranges::find(move.bodies, key); existing != move.bodies.end()) {
        move.bodies.erase(existing);
    } else {
        move.bodies.push_back(key);
    }
    Bnd_Box box;
    for (const core::BodyKey& selected : move.bodies) {
        if (const core::Body* body = m_baseModel.body(selected)) {
            BRepBndLib::Add(body->shape, box);
        }
    }
    if (!box.IsVoid()) {
        move.pivot = gp_Pnt((box.CornerMin().XYZ() + box.CornerMax().XYZ()) * 0.5);
    }
}

void FeaturePanel::resolveHole(const core::HoleFeature& hole, Resolved& resolved) const
{
    if (hole.sketch != 0) {
        const auto state = m_baseModel.sketches.find(hole.sketch);
        if (state == m_baseModel.sketches.end()) {
            resolved.items.push_back(tr("The sketch is missing"));
            return;
        }
        const gp_Ax3& plane = state->second.plane;
        const gp_Dir drill = hole.flip ? plane.Direction() : plane.Direction().Reversed();
        for (std::size_t i = 0; i < hole.sketchPoints.size(); ++i) {
            if (const sketch::Point* point = state->second.solved.point(hole.sketchPoints[i])) {
                resolved.holeAxes.emplace_back(sketch::toWorld(plane, point->pos), drill);
                resolved.items.push_back(tr("Sketch point %1").arg(i + 1));
            } else {
                resolved.items.push_back(tr("Sketch point %1 is missing").arg(i + 1));
            }
        }
    } else {
        for (std::size_t i = 0; i < hole.facePoints.size(); ++i) {
            if (const std::optional<gp_Ax1> axis = core::holeAxis(m_baseModel.bodies, hole.facePoints[i])) {
                resolved.holeAxes.push_back(*axis);
                resolved.items.push_back(tr("Hole %1").arg(i + 1));
            } else {
                resolved.items.push_back(tr("Hole %1 is missing").arg(i + 1));
            }
        }
    }
    resolved.complete = !resolved.holeAxes.empty();
}

void FeaturePanel::buildOffsetFaceForm(core::OffsetFaceFeature& f, QFormLayout* fields)
{
    m_size = makeLengthBox(f.distance, -kMaxLength, m_form);
    m_size->setToolTip(tr("Negative distances remove material"));
    auto* clear = new QPushButton(tr("Clear faces"), m_form);
    fields->addRow(tr("Distance"), m_size);
    fields->addRow(QString(), clear);
    connect(m_size, &ValueEdit::valueChanged, this, [this](double value) {
        if (std::abs(value) >= kMinSize) {
            std::get<core::OffsetFaceFeature>(*m_data).distance = value;
            updatePreview();
        }
    });
    connect(m_size, &ValueEdit::accepted, this, &FeaturePanel::accept);
    connect(clear, &QPushButton::clicked, this, [this] {
        std::get<core::OffsetFaceFeature>(*m_data).faces.clear();
        updatePreview();
    });
}

void FeaturePanel::buildHoleForm(core::HoleFeature& f, QFormLayout* fields)
{
    auto* kind = new QComboBox(m_form);
    kind->addItems({tr("Simple"), tr("Counterbore"), tr("Countersink")});
    kind->setCurrentIndex(static_cast<int>(f.kind));
    auto* diameter = makeLengthBox(f.diameter, kMinSize, m_form);
    m_size = makeLengthBox(f.depth, kMinSize, m_form);
    auto* through = new QCheckBox(tr("Through all"), m_form);
    through->setChecked(f.throughAll);
    auto* headDiameter = makeLengthBox(f.headDiameter, kMinSize, m_form);
    auto* headDepth = makeLengthBox(f.headDepth, kMinSize, m_form);
    auto* sinkAngle = makeAngleBox(f.countersinkAngle, kMinConeAngle, kMaxConeAngle, m_form);
    auto* tipAngle = makeAngleBox(f.tipAngle, 0.0, kMaxConeAngle, m_form);
    tipAngle->setToolTip(tr("0 for a flat bottom"));
    auto* flip = new QCheckBox(tr("Flip direction"), m_form);
    flip->setChecked(f.flip);
    flip->setToolTip(tr("Drills along the sketch normal, for holes on sketch points"));
    fields->addRow(tr("Type"), kind);
    fields->addRow(tr("Diameter"), diameter);
    fields->addRow(tr("Depth"), m_size);
    fields->addRow(QString(), through);
    fields->addRow(tr("Head diameter"), headDiameter);
    fields->addRow(tr("Head depth"), headDepth);
    fields->addRow(tr("Countersink angle"), sinkAngle);
    fields->addRow(tr("Tip angle"), tipAngle);
    fields->addRow(QString(), flip);
    const auto showRows = [fields, headDiameter, headDepth, sinkAngle, tipAngle,
                           depth = m_size](core::HoleKind holeKind, bool throughAll) {
        fields->setRowVisible(headDiameter, holeKind != core::HoleKind::Simple);
        fields->setRowVisible(headDepth, holeKind == core::HoleKind::Counterbore);
        fields->setRowVisible(sinkAngle, holeKind == core::HoleKind::Countersink);
        fields->setRowVisible(depth, !throughAll);
        fields->setRowVisible(tipAngle, !throughAll);
    };
    showRows(f.kind, f.throughAll);
    const auto apply = [this, kind, diameter, through, headDiameter, headDepth, sinkAngle, tipAngle, flip, showRows] {
        auto& data = std::get<core::HoleFeature>(*m_data);
        data.kind = static_cast<core::HoleKind>(kind->currentIndex());
        data.diameter = diameter->value();
        data.depth = m_size->value();
        data.throughAll = through->isChecked();
        data.headDiameter = headDiameter->value();
        data.headDepth = headDepth->value();
        data.countersinkAngle = sinkAngle->value();
        data.tipAngle = tipAngle->value();
        data.flip = flip->isChecked();
        showRows(data.kind, data.throughAll);
        updatePreview();
    };
    for (ValueEdit* box : {diameter, m_size, headDiameter, headDepth, sinkAngle, tipAngle}) {
        connect(box, &ValueEdit::valueChanged, this, apply);
        connect(box, &ValueEdit::accepted, this, &FeaturePanel::accept);
    }
    connect(kind, &QComboBox::currentIndexChanged, this, apply);
    connect(through, &QCheckBox::toggled, this, apply);
    connect(flip, &QCheckBox::toggled, this, apply);
}

void FeaturePanel::buildMoveForm(core::MoveFeature& f, QFormLayout* fields)
{
    const double values[] = {f.translation.X(), f.translation.Y(), f.translation.Z()};
    const QString labels[] = {tr("X distance"), tr("Y distance"), tr("Z distance")};
    for (std::size_t i = 0; i < m_offsets.size(); ++i) {
        m_offsets[i] = makeLengthBox(values[i], -kMaxLength, m_form);
        fields->addRow(labels[i], m_offsets[i]);
    }
    auto* axis = new QComboBox(m_form);
    axis->addItems({tr("X axis"), tr("Y axis"), tr("Z axis")});
    axis->setCurrentIndex(static_cast<int>(f.rotateAxis) - static_cast<int>(core::AxisKind::X));
    auto* angle = makeAngleBox(f.angleDegrees, -360.0, 360.0, m_form);
    auto* copy = new QCheckBox(tr("Create copy"), m_form);
    copy->setChecked(f.copy);
    copy->setToolTip(tr("Leaves the bodies where they are and moves new copies"));
    fields->addRow(tr("Rotate about"), axis);
    fields->addRow(tr("Angle"), angle);
    fields->addRow(QString(), copy);
    const auto apply = [this, axis, angle, copy] {
        auto& data = std::get<core::MoveFeature>(*m_data);
        data.translation = gp_Vec(m_offsets[0]->value(), m_offsets[1]->value(), m_offsets[2]->value());
        data.rotateAxis = static_cast<core::AxisKind>(axis->currentIndex() + static_cast<int>(core::AxisKind::X));
        data.angleDegrees = angle->value();
        data.copy = copy->isChecked();
        updatePreview();
    };
    for (ValueEdit* box : {m_offsets[0], m_offsets[1], m_offsets[2], angle}) {
        connect(box, &ValueEdit::valueChanged, this, apply);
        connect(box, &ValueEdit::accepted, this, &FeaturePanel::accept);
    }
    connect(axis, &QComboBox::currentIndexChanged, this, apply);
    connect(copy, &QCheckBox::toggled, this, apply);
}

void FeaturePanel::toggleAppearanceTarget(core::AppearanceFeature& appearance, const Pick& pick)
{
    if (pick.kind != Pick::Kind::Face) {
        return;
    }
    if (m_appearanceFaces) {
        toggleFace(appearance.faces, pick);
        return;
    }
    const core::BodyKey key = pick.body();
    if (m_baseModel.body(key) == nullptr) {
        emit message(tr("Pick a body that exists before this feature"));
        return;
    }
    if (auto existing = std::ranges::find(appearance.bodies, key); existing != appearance.bodies.end()) {
        appearance.bodies.erase(existing);
    } else {
        appearance.bodies.push_back(key);
    }
}

void FeaturePanel::buildAppearanceForm(core::AppearanceFeature& f, QFormLayout* fields)
{
    auto* target = new QComboBox(m_form);
    target->addItems({tr("Bodies"), tr("Faces")});
    target->setCurrentIndex(m_appearanceFaces ? 1 : 0);
    target->setToolTip(tr("A color on a body replaces the colors of its faces"));

    auto* swatches = new QWidget(m_form);
    auto* row = new QGridLayout(swatches);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(theme::kSpaceS);
    auto* custom = new QPushButton(tr("Custom..."), m_form);
    auto* clear = new QPushButton(tr("Default"), m_form);
    clear->setToolTip(tr("Puts the bodies and faces back to the color they came with"));

    fields->addRow(tr("Apply to"), target);
    fields->addRow(tr("Color"), swatches);
    fields->addRow(QString(), custom);
    fields->addRow(QString(), clear);

    const auto setColor = [this, custom](const std::optional<geometry::Color>& color) {
        auto& data = std::get<core::AppearanceFeature>(*m_data);
        data.color = color;
        data.clear = !color.has_value();
        paintSwatch(custom, color);
        updatePreview();
    };
    paintSwatch(custom, f.color);

    const int columns = 4;
    for (int i = 0; i < static_cast<int>(std::size(kPresets)); ++i) {
        const Preset& preset = kPresets[i];
        auto* swatch = new QPushButton(swatches);
        swatch->setFixedSize(kSwatchSize, kSwatchSize);
        swatch->setToolTip(tr(preset.name));
        swatch->setStyleSheet(QStringLiteral("background-color: %1").arg(preset.color.name()));
        row->addWidget(swatch, i / columns, i % columns);
        connect(swatch, &QPushButton::clicked, this, [setColor, color = preset.color] { setColor(toColor(color)); });
    }
    connect(custom, &QPushButton::clicked, this, [this, setColor] {
        const auto& data = std::get<core::AppearanceFeature>(*m_data);
        const QColor picked =
            QColorDialog::getColor(data.color ? toQColor(*data.color) : theme::kTitanium, this, tr("Appearance"));
        if (picked.isValid()) {
            setColor(toColor(picked));
        }
    });
    connect(clear, &QPushButton::clicked, this, [setColor] { setColor(std::nullopt); });
    connect(target, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_appearanceFaces = index == 1;
        auto& data = std::get<core::AppearanceFeature>(*m_data);
        if (m_appearanceFaces) {
            data.bodies.clear();
        } else {
            data.faces.clear();
        }
        updatePreview();
    });
}

} // namespace kalve137::app
