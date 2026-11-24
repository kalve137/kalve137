// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/sketch_snapshots.h"

#include "app/session/design_session.h"
#include "app/theme/theme.h"
#include "app/view/scene.h"
#include "sketch/profiles.h"
#include "viewer/style.h"

#include <QColor>

#include <utility>
#include <variant>

namespace kalve137::app {

namespace {

constexpr double kProfileTransparency = viewer::style::kTintFaint;
constexpr double kHighlightTransparency = viewer::style::kTintStrong;
constexpr double kHoverTransparency = viewer::style::kTintStrong;

snapshot::Color colorOf(const QColor& color, double transparency = 0.0)
{
    return {static_cast<float>(color.redF()), static_cast<float>(color.greenF()), static_cast<float>(color.blueF()),
            static_cast<float>(1.0 - transparency)};
}

} // namespace

std::vector<snapshot::Sketch> sketchSnapshots(const core::Document& document, const core::Model& model,
                                              const DesignSession& session)
{
    std::vector<snapshot::Sketch> sketches;
    for (const core::Feature& feature : document.features()) {
        const auto* data = std::get_if<core::SketchFeature>(&feature.data);
        const auto state = model.sketches.find(feature.id);
        if (data == nullptr || state == model.sketches.end() || !session.isVisible(feature.id)) {
            continue;
        }
        snapshot::Sketch shown =
            sketch::snapshotOf(data->sketch, state->second.plane, state->second.profiles, feature.id);
        shown.points = sketch::pointsOf(state->second.solved, sketch::placementOf(state->second.plane));
        sketches.push_back(std::move(shown));
    }
    return sketches;
}

render::SketchLook sketchLook(const Scene& scene)
{
    const theme::Palette& colors = theme::palette();
    const PickMode mode = scene.pickMode();
    render::SketchLook look;
    look.free = colorOf(colors.viewport.sketchFree);
    look.construction = colorOf(colors.viewport.sketchConstruction);
    look.profile = colorOf(colors.viewport.plane, kProfileTransparency);
    look.highlight = colorOf(colors.chrome.accent, kHighlightTransparency);
    look.hover = colorOf(colors.viewport.hover, kHoverTransparency);
    look.editing = static_cast<int>(scene.editingSketch());
    look.lines = mode == PickMode::Lines || mode == PickMode::ProfilesAndLines;
    look.points = mode == PickMode::HolePlacement;
    look.profiles = mode == PickMode::Profiles || mode == PickMode::ProfilesAndLines || mode == PickMode::PressPull;
    for (const auto& [feature, index] : scene.highlightedProfiles()) {
        look.highlighted.emplace(static_cast<int>(feature), index);
    }
    return look;
}

} // namespace kalve137::app
