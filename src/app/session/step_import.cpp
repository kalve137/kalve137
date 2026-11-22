// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/step_import.h"

#include "geometry/body_mesh.h"
#include "geometry/meshing.h"

#include <QString>

#include <format>
#include <memory>
#include <string>
#include <utility>

namespace kalve137::app {

namespace {

constexpr double kReadShare = 0.75;

} // namespace

core::ImportFeature importFeature(const std::filesystem::path& path, std::vector<geometry::StepPart> parts)
{
    const std::string name = QString::fromStdU16String(path.stem().u16string()).toStdString();
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].name.empty()) {
            parts[i].name = parts.size() == 1 ? name : std::format("{} {}", name, i + 1);
        }
    }
    return {QString::fromStdU16String(path.filename().u16string()).toStdString(), std::move(parts)};
}

ImportedStep readStepForImport(const std::filesystem::path& path, const geometry::ProgressCallback& progress)
{
    const auto report = [&](double done) { return !progress || progress(done); };
    ImportedStep imported{.parts =
                              geometry::readStepParts(path, [&](double done) { return report(done * kReadShare); })};
    const std::size_t count = imported.parts.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (!report(kReadShare + (1.0 - kReadShare) * static_cast<double>(i) / static_cast<double>(count))) {
            throw geometry::ImportCanceled();
        }
        const TopoDS_Shape& shape = imported.parts[i].shape;
        geometry::ensureMesh(shape);
        imported.meshes.push_back(std::make_shared<const snapshot::Mesh>(geometry::meshBody(shape)));
    }
    return imported;
}

} // namespace kalve137::app
