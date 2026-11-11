// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "geometry/face_colors.h"

#include <TopoDS_Shape.hxx>

#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kalve137::geometry {

struct StepPart {
    std::string name;
    TopoDS_Shape shape;
    std::optional<Color> color;
    FaceColors faceColors;
    std::vector<std::string> path;
};

void writeStep(const std::vector<StepPart>& parts, const std::filesystem::path& path,
               const std::string& application = "Kalve 137");
void writeStl(const std::vector<TopoDS_Shape>& shapes, const std::filesystem::path& path);

using ProgressCallback = std::function<bool(double)>;

struct ImportCanceled : std::runtime_error {
    ImportCanceled() : std::runtime_error("The import was canceled") {}
};

std::vector<StepPart> readStepParts(const std::filesystem::path& path, const ProgressCallback& progress = {});

std::string toBrep(const TopoDS_Shape& shape);
TopoDS_Shape fromBrep(const std::string& text);

std::string toBinaryBrep(const TopoDS_Shape& shape);
TopoDS_Shape fromBinaryBrep(const std::string& bytes);

} // namespace kalve137::geometry
