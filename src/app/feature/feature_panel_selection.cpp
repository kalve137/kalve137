// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/feature_panel.h"

#include "app/feature/feature_panel_detail.h"
#include "app/session/design_session.h"

#include "core/naming.h"

#include "geometry/modeling.h"
#include "geometry/shapes.h"
#include "geometry/topology.h"
#include "log/log.h"
#include "sketch/profiles.h"
#include "viewer/occt_view_widget.h"

#include <TopoDS.hxx>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <variant>

namespace kalve137::app {

using feature_panel_detail::counted;
using feature_panel_detail::kMatchTolerance;

FeaturePanel::Resolved FeaturePanel::resolve() const
{
    Resolved resolved;
    const auto collectProfiles = [&](const core::ProfileRef& ref) {
        auto state = m_baseModel.sketches.find(ref.sketch);
        if (state == m_baseModel.sketches.end()) {
            return;
        }
        for (std::size_t i = 0; i < ref.seeds.size(); ++i) {
            const auto index = sketch::profileAt(state->second.profiles, state->second.plane, ref.seeds[i]);
            if (!index) {
                resolved.items.push_back(tr("Profile %1 is missing").arg(i + 1));
                continue;
            }
            resolved.profiles.insert({ref.sketch, *index});
            resolved.items.push_back(
                tr("Profile %1   %2 mm²").arg(i + 1).arg(geometry::area(state->second.profiles[*index]), 0, 'f', 2));
        }
        resolved.complete = !ref.seeds.empty();
    };
    const auto collectEdges = [&](const std::vector<core::TopoRef>& refs) {
        for (std::size_t i = 0; i < refs.size(); ++i) {
            if (const std::optional<core::EdgeHit> hit = core::resolveEdge(m_baseModel.bodies, refs[i])) {
                resolved.items.push_back(tr("Edge %1   %2 mm").arg(i + 1).arg(geometry::length(hit->edge), 0, 'f', 2));
                resolved.edges.push_back(hit->edge);
                resolved.lastEdgeBody = m_baseModel.bodies[hit->body].shape;
            } else {
                resolved.items.push_back(tr("Edge %1 is missing").arg(i + 1));
            }
        }
        resolved.complete = !refs.empty();
    };
    const auto collectFaces = [&](const std::vector<core::TopoRef>& refs) {
        for (std::size_t i = 0; i < refs.size(); ++i) {
            if (const std::optional<core::FaceHit> hit = core::resolveFace(m_baseModel.bodies, refs[i])) {
                resolved.items.push_back(tr("Face %1   %2 mm²").arg(i + 1).arg(geometry::area(hit->face), 0, 'f', 2));
                resolved.faces.push_back(hit->face);
            } else {
                resolved.items.push_back(tr("Face %1 is missing").arg(i + 1));
            }
        }
        resolved.complete = !refs.empty();
    };
    std::visit(
        core::Overloaded{
            [](const core::SketchFeature&) {},
            [&](const core::ExtrudeFeature& f) {
                collectProfiles(f.profiles);
                if (f.end == core::ExtrudeEnd::ToObject) {
                    if (f.toFace) {
                        if (const auto hit = core::resolveFace(m_baseModel.bodies, *f.toFace)) {
                            resolved.faces.push_back(hit->face);
                        }
                    }
                    resolved.complete = resolved.complete && (f.toFace || f.toPlane != 0);
                }
            },
            [&](const core::RevolveFeature& f) { collectProfiles(f.profiles); },
            [&](const core::FilletFeature& f) {
                const core::FilletSet& active = activeSet(f);
                for (const core::FilletSet& set : f.sets) {
                    if (&set == &active) {
                        continue;
                    }
                    for (const core::TopoRef& ref : set.edges) {
                        if (const std::optional<core::EdgeHit> hit = core::resolveEdge(m_baseModel.bodies, ref)) {
                            resolved.edges.push_back(hit->edge);
                        }
                    }
                }
                collectEdges(active.edges);
                resolved.complete = std::ranges::any_of(f.sets, [](const auto& set) { return !set.edges.empty(); });
            },
            [&](const core::ChamferFeature& f) { collectEdges(f.edges); },
            [&](const core::ShellFeature& f) { collectFaces(f.faces); },
            [&](const core::PatternFeature& f) { resolved.complete = f.body.feature != 0; },
            [](const core::ImportFeature&) {},
            [&](const core::PlaneFeature& f) { resolvePlane(f, resolved); },
            [&](const core::OffsetFaceFeature& f) { collectFaces(f.faces); },
            [&](const core::HoleFeature& f) { resolveHole(f, resolved); },
            [&](const core::MoveFeature& f) {
                for (const core::BodyKey& key : f.bodies) {
                    const core::Body* body = m_baseModel.body(key);
                    resolved.items.push_back(body != nullptr ? QString::fromStdString(body->name) : tr("Missing body"));
                }
                resolved.complete = !f.bodies.empty();
            },
            [&](const core::AxisFeature& f) {
                if (m_firstAxisPoint) {
                    resolved.items.push_back(tr("First point"));
                } else if (!f.baseName.empty()) {
                    resolved.items.push_back(QString::fromStdString(f.baseName));
                }
                resolved.complete = !f.baseName.empty() && !m_firstAxisPoint;
            },
            [&](const core::PointFeature& f) {
                if (!f.baseName.empty()) {
                    resolved.items.push_back(QString::fromStdString(f.baseName));
                }
                resolved.complete = !f.baseName.empty();
            },
            [&](const core::AppearanceFeature& f) {
                for (const core::BodyKey& key : f.bodies) {
                    const core::Body* body = m_baseModel.body(key);
                    resolved.items.push_back(body != nullptr ? QString::fromStdString(body->name) : tr("Missing body"));
                }
                collectFaces(f.faces);
                resolved.complete = (!f.bodies.empty() || !f.faces.empty()) && (f.color || f.clear);
            },
        },
        *m_data);
    return resolved;
}

void FeaturePanel::onSelectionChanged()
{
    if (!isActive()) {
        return;
    }
    const std::vector<Pick> picks = m_scene.takePicks();
    if (picks.empty()) {
        return;
    }
    const QScopedValueRollback addOnly(m_addOnly, m_view->selectedByBox());
    for (const Pick& pick : picks) {
        K137_DEBUG(Input, "Feature panel pick kind {} feature {} index {}", static_cast<int>(pick.kind), pick.feature,
                   pick.index);
        togglePick(pick);
    }
    updatePreview();
}

void FeaturePanel::togglePick(const Pick& pick)
{
    if (m_pressPull && handOffPressPull(pick)) {
        return;
    }
    const auto showAxis = [this](const core::AxisRef& axis, int firstKind = 0) {
        const QSignalBlocker blocker(m_axis);
        m_axis->setCurrentIndex(static_cast<int>(axis.kind) - firstKind);
    };
    std::visit(core::Overloaded{
                   [](core::SketchFeature&) {},
                   [&](core::ExtrudeFeature& f) {
                       if (m_pickingTarget) {
                           pickExtrudeTarget(f, pick);
                       } else {
                           toggleProfile(f.profiles, pick);
                       }
                   },
                   [&](core::RevolveFeature& f) {
                       if (pick.kind == Pick::Kind::SketchLine) {
                           f.axis = {core::AxisKind::SketchLine, pick.feature, pick.index};
                           showAxis(f.axis);
                           return;
                       }
                       if (pickAxisRef(f.axis, pick)) {
                           showAxis(f.axis);
                           return;
                       }
                       toggleProfile(f.profiles, pick);
                   },
                   [&](core::FilletFeature& f) { toggleEdge(activeSet(f).edges, pick); },
                   [&](core::ChamferFeature& f) { toggleEdge(f.edges, pick); },
                   [&](core::ShellFeature& f) { toggleFace(f.faces, pick); },
                   [&](core::PatternFeature& f) {
                       if (pickAxisRef(f.axis, pick)) {
                           showAxis(f.axis, static_cast<int>(core::AxisKind::X));
                           return;
                       }
                       if (pick.kind == Pick::Kind::Face && m_baseModel.body(pick.body()) != nullptr) {
                           f.body = pick.body();
                       }
                   },
                   [](core::ImportFeature&) {},
                   [&](core::PlaneFeature& f) {
                       switch (f.kind) {
                       case core::PlaneKind::Offset:
                           pickPlaneBase(f, pick);
                           return;
                       case core::PlaneKind::Angle:
                           pickHinge(f, pick);
                           return;
                       case core::PlaneKind::Midplane:
                           pickMidplane(f, pick);
                           return;
                       }
                   },
                   [&](core::OffsetFaceFeature& f) { toggleFace(f.faces, pick); },
                   [&](core::HoleFeature& f) { toggleHolePosition(f, pick); },
                   [&](core::MoveFeature& f) { toggleBody(f, pick); },
                   [&](core::AxisFeature& f) { pickAxisSource(f, pick); },
                   [&](core::PointFeature& f) { pickPointSource(f, pick); },
                   [&](core::AppearanceFeature& f) { toggleAppearanceTarget(f, pick); },
               },
               *m_data);
}

bool FeaturePanel::pickAxisRef(core::AxisRef& axis, const Pick& pick)
{
    switch (pick.kind) {
    case Pick::Kind::OriginAxis:
        axis = {pick.axis, 0, 0};
        return true;
    case Pick::Kind::ConstructionAxis:
        if (m_base.find(pick.feature) == nullptr || pick.feature == m_editing) {
            emit message(tr("Pick an axis made before this feature"));
            return true;
        }
        axis = {core::AxisKind::Construction, 0, 0, pick.feature};
        return true;
    case Pick::Kind::OriginPlane:
    case Pick::Kind::Face:
    case Pick::Kind::Edge:
    case Pick::Kind::Vertex:
    case Pick::Kind::Profile:
    case Pick::Kind::SketchLine:
    case Pick::Kind::SketchPoint:
    case Pick::Kind::ConstructionPlane:
    case Pick::Kind::ConstructionPoint:
        return false;
    }
    return false;
}

std::optional<FeaturePanel::PlaneSource> FeaturePanel::planeSourceOf(const Pick& pick)
{
    const std::optional<gp_Ax3> frame = sketchPlaneOf(pick);
    if (!frame) {
        return std::nullopt;
    }
    switch (pick.kind) {
    case Pick::Kind::OriginPlane:
        return PlaneSource{*frame, std::string(core::toString(pick.plane)), gp::Origin()};
    case Pick::Kind::ConstructionPlane: {
        const core::Feature* source = m_base.find(pick.feature);
        if (source == nullptr || pick.feature == m_editing) {
            emit message(tr("Pick a plane made before this one"));
            return std::nullopt;
        }
        return PlaneSource{*frame, source->name, pick.construction.center};
    }
    case Pick::Kind::Face:
        return PlaneSource{*frame, "Face", geometry::areaCentroid(pick.shape)};
    case Pick::Kind::Edge:
    case Pick::Kind::Vertex:
    case Pick::Kind::Profile:
    case Pick::Kind::SketchLine:
    case Pick::Kind::SketchPoint:
    case Pick::Kind::OriginAxis:
    case Pick::Kind::ConstructionAxis:
    case Pick::Kind::ConstructionPoint:
        return std::nullopt;
    }
    return std::nullopt;
}

void FeaturePanel::pickPlaneBase(core::PlaneFeature& plane, const Pick& pick)
{
    const std::optional<PlaneSource> source = planeSourceOf(pick);
    if (!source) {
        if (pick.kind != Pick::Kind::ConstructionPlane) {
            emit message(tr("Offset planes need an origin plane, a construction plane or a flat face"));
        }
        return;
    }
    plane.base = source->frame;
    plane.baseName = source->name;
    plane.center = source->center;
}

void FeaturePanel::pickExtrudeTarget(core::ExtrudeFeature& extrude, const Pick& pick)
{
    if (pick.kind == Pick::Kind::ConstructionPlane) {
        if (m_base.find(pick.feature) == nullptr || pick.feature == m_editing) {
            emit message(tr("Pick a plane made before this extrude"));
            return;
        }
        extrude.toPlane = pick.feature;
        extrude.toFace.reset();
    } else if (pick.kind == Pick::Kind::Face && !pick.shape.IsNull() && pick.shape.ShapeType() == TopAbs_FACE) {
        const TopoDS_Face& face = TopoDS::Face(pick.shape);
        if (!geometry::planarFrame(face)) {
            emit message(tr("Pick a flat face or a construction plane"));
            return;
        }
        const gp_Pnt picked = geometry::facePoint(face);
        const auto owner = std::ranges::find_if(m_baseModel.bodies, [&](const core::Body& body) {
            return geometry::findFace(body.shape, picked, kMatchTolerance).has_value();
        });
        if (owner == m_baseModel.bodies.end()) {
            emit message(tr("Pick a face of a body made before this extrude"));
            return;
        }
        const TopoDS_Face base = *geometry::findFace(owner->shape, picked, kMatchTolerance);
        extrude.toFace = core::faceRef(*owner, base, picked);
        extrude.toPlane = 0;
    } else {
        emit message(tr("Pick a flat face or a construction plane"));
        return;
    }
    if (m_target != nullptr) {
        m_target->setText(extrudeTargetName(extrude));
    }
    setPickingTarget(false);
}

QString FeaturePanel::extrudeTargetName(const core::ExtrudeFeature& extrude) const
{
    if (extrude.toPlane != 0) {
        const core::Feature* plane = m_base.find(extrude.toPlane);
        return plane != nullptr ? QString::fromStdString(plane->name) : tr("Missing plane");
    }
    return extrude.toFace ? tr("Face") : tr("Pick a face or plane");
}

void FeaturePanel::toggleProfile(core::ProfileRef& ref, const Pick& pick)
{
    if (pick.kind != Pick::Kind::Profile) {
        return;
    }
    auto state = m_baseModel.sketches.find(pick.feature);
    if (state == m_baseModel.sketches.end() || pick.index < 0 ||
        static_cast<std::size_t>(pick.index) >= state->second.profiles.size()) {
        return;
    }
    const auto& profiles = state->second.profiles;
    const gp_Ax3& plane = state->second.plane;
    const auto index = static_cast<std::size_t>(pick.index);
    if (ref.sketch != pick.feature) {
        ref.sketch = pick.feature;
        ref.seeds.clear();
    }
    auto existing = std::ranges::find_if(
        ref.seeds, [&](const sketch::Vec2& seed) { return sketch::profileAt(profiles, plane, seed) == index; });
    if (existing != ref.seeds.end()) {
        ref.seeds.erase(existing);
        return;
    }
    if (const auto seed = sketch::interiorPoint(profiles[index], plane)) {
        ref.seeds.push_back(*seed);
    }
}

void FeaturePanel::toggleEdge(std::vector<core::TopoRef>& refs, const Pick& pick)
{
    if (pick.kind != Pick::Kind::Edge || pick.shape.IsNull() || pick.shape.ShapeType() != TopAbs_EDGE) {
        return;
    }
    const gp_Pnt picked = geometry::edgeMidpoint(TopoDS::Edge(pick.shape));
    for (std::size_t i = 0; i < m_baseModel.bodies.size(); ++i) {
        if (const auto base = geometry::findEdge(m_baseModel.bodies[i].shape, picked, kMatchTolerance)) {
            toggleEdgeRun(refs, i, *base);
            return;
        }
    }
    if (m_addOnly) {
        return;
    }
    if (const auto source = blendSourceOf(refs, pick.shape)) {
        K137_DEBUG(Input, "Pick on a blend face maps back to its source edge");
        toggleEdgeRun(refs, source->first, source->second);
        return;
    }
    emit message(tr("Pick an edge of the original body, not one this feature creates"));
}

void FeaturePanel::toggleEdgeRun(std::vector<core::TopoRef>& refs, std::size_t bodyIndex, const TopoDS_Edge& edge)
{
    const core::Body& body = m_baseModel.bodies[bodyIndex];
    std::vector<TopoDS_Edge> run =
        m_tangentChain ? geometry::tangentChain(body.shape, edge) : std::vector<TopoDS_Edge>{edge};
    std::ranges::stable_partition(run, [&](const TopoDS_Edge& member) { return !member.IsSame(edge); });
    const auto refOf = [&](const TopoDS_Edge& target) {
        return std::ranges::find_if(refs, [&](const core::TopoRef& ref) {
            const std::optional<core::EdgeHit> hit = core::resolveEdge(m_baseModel.bodies, ref);
            return hit && hit->body == bodyIndex && hit->edge.IsSame(target);
        });
    };
    const bool selected = refOf(edge) != refs.end();
    if (selected && m_addOnly) {
        return;
    }
    for (const TopoDS_Edge& member : run) {
        auto existing = refOf(member);
        if (selected && existing != refs.end()) {
            refs.erase(existing);
        } else if (!selected && existing == refs.end()) {
            refs.push_back(core::edgeRef(body, member, geometry::edgeMidpoint(member)));
        }
    }
}

std::optional<std::pair<std::size_t, TopoDS_Edge>> FeaturePanel::blendSourceOf(const std::vector<core::TopoRef>& refs,
                                                                               const TopoDS_Shape& picked) const
{
    std::optional<std::pair<geometry::Blend, double>> blend;
    if (const auto* fillet = std::get_if<core::FilletFeature>(&*m_data)) {
        blend = {geometry::Blend::Fillet, activeSet(*fillet).radius};
    } else if (const auto* chamfer = std::get_if<core::ChamferFeature>(&*m_data)) {
        blend = {geometry::Blend::Chamfer, chamfer->distance};
    }
    if (!blend) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < m_baseModel.bodies.size(); ++i) {
        std::vector<TopoDS_Edge> edges;
        for (const core::TopoRef& ref : refs) {
            if (const std::optional<core::EdgeHit> hit = core::resolveEdge(m_baseModel.bodies, ref);
                hit && hit->body == i) {
                edges.push_back(hit->edge);
            }
        }
        const TopoDS_Shape& shape = m_baseModel.bodies[i].shape;
        if (auto source = geometry::blendSource(shape, edges, blend->second, blend->first, picked)) {
            return std::pair{i, *source};
        }
    }
    return std::nullopt;
}

void FeaturePanel::toggleFace(std::vector<core::TopoRef>& refs, const Pick& pick)
{
    if (pick.kind != Pick::Kind::Face || pick.shape.IsNull() || pick.shape.ShapeType() != TopAbs_FACE) {
        return;
    }
    const gp_Pnt picked = geometry::facePoint(TopoDS::Face(pick.shape));
    for (std::size_t i = 0; i < m_baseModel.bodies.size(); ++i) {
        const core::Body& body = m_baseModel.bodies[i];
        const std::optional<TopoDS_Face> base = geometry::findFace(body.shape, picked, kMatchTolerance);
        if (!base) {
            continue;
        }
        auto existing = std::ranges::find_if(refs, [&](const core::TopoRef& ref) {
            const std::optional<core::FaceHit> hit = core::resolveFace(m_baseModel.bodies, ref);
            return hit && hit->body == i && hit->face.IsSame(*base);
        });
        if (existing == refs.end()) {
            refs.push_back(core::faceRef(body, *base, geometry::facePoint(*base)));
        } else if (!m_addOnly) {
            refs.erase(existing);
        }
        return;
    }
    if (!m_addOnly) {
        emit message(tr("Pick a face of the original body, not one this feature creates"));
    }
}

void FeaturePanel::refreshItems(const std::vector<QString>& items)
{
    while (QLayoutItem* item = m_itemsLayout->takeAt(0)) {
        if (QWidget* row = item->widget()) {
            row->hide();
            row->deleteLater();
        }
        delete item;
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        auto* row = new QWidget(m_items);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(items[i], row);
        label->setObjectName("selectionItem");
        auto* remove = new QToolButton(row);
        remove->setObjectName("removeItem");
        remove->setText(QStringLiteral("×"));
        remove->setToolTip(tr("Remove from the selection"));
        remove->setAutoRaise(true);
        layout->addWidget(label, 1);
        layout->addWidget(remove);
        connect(remove, &QToolButton::clicked, this, [this, i] { removeItem(i); });
        m_itemsLayout->addWidget(row);
    }
    m_items->setVisible(!items.empty());
}

void FeaturePanel::removeItem(std::size_t index)
{
    if (!isActive()) {
        return;
    }
    const auto eraseAt = [index](auto& list) {
        if (index < list.size()) {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
        }
    };
    std::visit(core::Overloaded{
                   [](core::SketchFeature&) {},
                   [&](core::ExtrudeFeature& f) { eraseAt(f.profiles.seeds); },
                   [&](core::RevolveFeature& f) { eraseAt(f.profiles.seeds); },
                   [&](core::FilletFeature& f) { eraseAt(activeSet(f).edges); },
                   [&](core::ChamferFeature& f) { eraseAt(f.edges); },
                   [&](core::ShellFeature& f) { eraseAt(f.faces); },
                   [](core::PatternFeature&) {},
                   [](core::ImportFeature&) {},
                   [index](core::PlaneFeature& f) {
                       if (f.kind == core::PlaneKind::Midplane && index == 0 && !f.secondName.empty()) {
                           f.base = f.second;
                           f.baseName = f.secondName;
                           f.center = f.secondCenter;
                           f.secondName.clear();
                       } else if (index == 1) {
                           f.secondName.clear();
                       } else {
                           f.baseName.clear();
                       }
                   },
                   [&](core::OffsetFaceFeature& f) { eraseAt(f.faces); },
                   [&](core::HoleFeature& f) {
                       if (f.sketch != 0) {
                           eraseAt(f.sketchPoints);
                           if (f.sketchPoints.empty()) {
                               f.sketch = 0;
                           }
                       } else {
                           eraseAt(f.facePoints);
                       }
                   },
                   [&](core::MoveFeature& f) { eraseAt(f.bodies); },
                   [this](core::AxisFeature& f) {
                       f.baseName.clear();
                       m_firstAxisPoint.reset();
                   },
                   [](core::PointFeature& f) { f.baseName.clear(); },
                   [&](core::AppearanceFeature& f) {
                       if (m_appearanceFaces) {
                           eraseAt(f.faces);
                       } else {
                           eraseAt(f.bodies);
                       }
                   },
               },
               *m_data);
    K137_DEBUG(Input, "Removed selection item {}", index + 1);
    updatePreview();
}

void FeaturePanel::preselectProfile(core::ProfileRef& ref) const
{
    const auto& features = m_base.features();
    for (std::size_t i = std::min(m_base.marker(), features.size()); i-- > 0;) {
        const core::Feature& feature = features[i];
        auto state = m_baseModel.sketches.find(feature.id);
        if (state == m_baseModel.sketches.end() || state->second.profiles.empty() || !m_session.isVisible(feature.id)) {
            continue;
        }
        ref.sketch = feature.id;
        if (state->second.profiles.size() == 1) {
            if (const auto seed = sketch::interiorPoint(state->second.profiles.front(), state->second.plane)) {
                ref.seeds.push_back(*seed);
            }
        }
        return;
    }
}

core::AxisRef FeaturePanel::defaultAxis(const core::ProfileRef& ref) const
{
    const core::Feature* feature = m_base.find(ref.sketch);
    const auto* sketchData = feature != nullptr ? std::get_if<core::SketchFeature>(&feature->data) : nullptr;
    if (sketchData == nullptr) {
        return {core::AxisKind::Z, 0, 0};
    }
    for (const auto& [id, curve] : sketchData->sketch.curves()) {
        if (curve.construction && curve.kind == sketch::CurveKind::Line) {
            return {core::AxisKind::SketchLine, ref.sketch, id};
        }
    }
    const auto state = m_baseModel.sketches.find(ref.sketch);
    const gp_Dir normal = (state != m_baseModel.sketches.end() ? state->second.plane : sketchData->plane).Direction();
    const std::pair<core::AxisKind, gp_Dir> axes[] = {
        {core::AxisKind::Y, gp::DY()}, {core::AxisKind::Z, gp::DZ()}, {core::AxisKind::X, gp::DX()}};
    for (const auto& [kind, direction] : axes) {
        if (std::abs(normal.Dot(direction)) < 1e-6) {
            return {kind, 0, 0};
        }
    }
    return {core::AxisKind::Z, 0, 0};
}

QString FeaturePanel::summaryOf(const Resolved& resolved) const
{
    if (!resolved.profiles.empty()) {
        double total = 0.0;
        for (const auto& [sketchId, index] : resolved.profiles) {
            total += geometry::area(m_baseModel.sketches.at(sketchId).profiles.at(index));
        }
        return tr("%1 | Area : %2 mm²")
            .arg(counted(resolved.profiles.size(), tr("1 Profile"), tr("%1 Profiles")))
            .arg(total, 0, 'f', 2);
    }
    if (!resolved.faces.empty()) {
        double total = 0.0;
        for (const TopoDS_Face& face : resolved.faces) {
            total += geometry::area(face);
        }
        return tr("%1 | Area : %2 mm²")
            .arg(counted(resolved.faces.size(), tr("1 Face"), tr("%1 Faces")))
            .arg(total, 0, 'f', 2);
    }
    if (!resolved.edges.empty()) {
        double total = 0.0;
        for (const TopoDS_Edge& edge : resolved.edges) {
            total += geometry::length(edge);
        }
        return tr("%1 | Length : %2 mm")
            .arg(counted(resolved.edges.size(), tr("1 Edge"), tr("%1 Edges")))
            .arg(total, 0, 'f', 2);
    }
    return {};
}

QString FeaturePanel::selectionText() const
{
    return std::visit(
        core::Overloaded{
            [](const core::SketchFeature&) { return QString(); },
            [this](const core::ExtrudeFeature& f) {
                if (m_pickingTarget) {
                    return tr("Click a flat face or a construction plane to stop at");
                }
                return f.profiles.seeds.empty()
                           ? tr("Click profiles in the view")
                           : counted(f.profiles.seeds.size(), tr("1 profile selected"), tr("%1 profiles selected"));
            },
            [](const core::RevolveFeature& f) {
                QString text = f.profiles.seeds.empty() ? tr("Click profiles in the view")
                                                        : counted(f.profiles.seeds.size(), tr("1 profile selected"),
                                                                  tr("%1 profiles selected"));
                if (f.axis.kind == core::AxisKind::SketchLine) {
                    text += tr(", axis is a sketch line");
                }
                return text;
            },
            [](const core::FilletFeature& f) {
                std::size_t edges = 0;
                for (const core::FilletSet& set : f.sets) {
                    edges += set.edges.size();
                }
                if (edges == 0) {
                    return tr("Click edges in the view");
                }
                const QString text = counted(edges, tr("1 edge selected"), tr("%1 edges selected"));
                return f.sets.size() > 1 ? tr("%1 in %2 sets").arg(text).arg(f.sets.size()) : text;
            },
            [](const core::ChamferFeature& f) {
                return f.edges.empty() ? tr("Click edges in the view")
                                       : counted(f.edges.size(), tr("1 edge selected"), tr("%1 edges selected"));
            },
            [](const core::ShellFeature& f) {
                return f.faces.empty() ? tr("Click faces to open")
                                       : counted(f.faces.size(), tr("1 face open"), tr("%1 faces open"));
            },
            [this](const core::PatternFeature& f) {
                const core::Body* body = m_baseModel.body(f.body);
                return body == nullptr ? tr("Click a body to copy")
                                       : tr("Copies %1").arg(QString::fromStdString(body->name));
            },
            [](const core::ImportFeature& f) { return QString::fromStdString(f.source); },
            [](const core::PlaneFeature& f) {
                switch (f.kind) {
                case core::PlaneKind::Offset:
                    return f.baseName.empty() ? tr("Click a plane or a flat face") : QString();
                case core::PlaneKind::Angle:
                    return f.baseName.empty() ? tr("Click a sketch line, a straight edge or an axis") : QString();
                case core::PlaneKind::Midplane:
                    if (f.baseName.empty()) {
                        return tr("Click two planes or flat faces");
                    }
                    return f.secondName.empty() ? tr("Click a second plane or flat face") : QString();
                }
                return QString();
            },
            [this](const core::OffsetFaceFeature& f) {
                if (m_pressPull) {
                    return tr("Click a profile, a face or an edge");
                }
                return f.faces.empty() ? tr("Click flat faces to move")
                                       : counted(f.faces.size(), tr("1 face selected"), tr("%1 faces selected"));
            },
            [](const core::HoleFeature& f) {
                const std::size_t count = f.sketch != 0 ? f.sketchPoints.size() : f.facePoints.size();
                return count == 0 ? tr("Click a face or sketch points to place holes")
                                  : counted(count, tr("1 hole"), tr("%1 holes"));
            },
            [](const core::MoveFeature& f) {
                return f.bodies.empty() ? tr("Click bodies to move")
                                        : counted(f.bodies.size(), tr("1 body selected"), tr("%1 bodies selected"));
            },
            [this](const core::AxisFeature& f) {
                if (m_firstAxisPoint) {
                    return tr("Click a second point");
                }
                return f.baseName.empty() ? tr("Click an edge, a cylinder, a face or two points") : QString();
            },
            [](const core::PointFeature& f) {
                return f.baseName.empty() ? tr("Click a vertex, an edge, a circle or a face") : QString();
            },
            [this](const core::AppearanceFeature& f) {
                const std::size_t count = m_appearanceFaces ? f.faces.size() : f.bodies.size();
                if (count == 0) {
                    return m_appearanceFaces ? tr("Click faces to color") : tr("Click bodies to color");
                }
                QString text = m_appearanceFaces ? counted(count, tr("1 face selected"), tr("%1 faces selected"))
                                                 : counted(count, tr("1 body selected"), tr("%1 bodies selected"));
                if (!f.color && !f.clear) {
                    text += tr(", pick a color");
                }
                return text;
            },
        },
        *m_data);
}

} // namespace kalve137::app
