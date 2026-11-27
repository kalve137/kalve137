// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/batch_import.h"

#include "app/session/step_import.h"

#include "core/recompute.h"
#include "core/serialization.h"
#include "core/shape_store.h"
#include "geometry/exchange.h"
#include "geometry/shapes.h"
#include "log/log.h"
#include "platform/platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace kalve137::app {

namespace {

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

constexpr double kBytesPerKilobyte = 1024.0;
constexpr double kBytesPerMegabyte = kBytesPerKilobyte * kBytesPerKilobyte;
constexpr double kVolumeTolerance = 1e-3;
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kVolumeDigits = 12;
const char* const kResultFile = "result.json";

constexpr std::array kColumns{
    "file",
    "status",
    "stage",
    "error",
    "total_ms",
    "read_ms",
    "build_ms",
    "measure_ms",
    "peak_mb",
    "parts",
    "solids",
    "faces",
    "components",
    "colored",
    "volume",
    "failed",
    "save_ms",
    "saved_kb",
    "reopen_ms",
    "reopen_solids",
    "export_ms",
    "export_kb",
    "roundtrip_ms",
    "roundtrip_solids",
    "roundtrip_faces",
    "roundtrip_volume",
    "roundtrip",
    "min_x",
    "min_y",
    "min_z",
    "max_x",
    "max_y",
    "max_z",
};

struct Counts {
    int solids = 0;
    int faces = 0;
    double volume = 0.0;
    Bnd_Box box;
};

QString toQString(const std::filesystem::path& path)
{
    return QDir::toNativeSeparators(QString::fromStdU16String(path.u16string()));
}

Counts countOf(const std::vector<TopoDS_Shape>& shapes)
{
    Counts counts;
    for (const TopoDS_Shape& shape : shapes) {
        ShapeMap solids;
        ShapeMap faces;
        TopExp::MapShapes(shape, TopAbs_SOLID, solids);
        TopExp::MapShapes(shape, TopAbs_FACE, faces);
        counts.solids += solids.Extent();
        counts.faces += faces.Extent();
        for (int i = 1; i <= solids.Extent(); ++i) {
            counts.volume += geometry::volume(solids(i));
        }
        BRepBndLib::AddOptimal(shape, counts.box, false, false);
    }
    return counts;
}

std::vector<TopoDS_Shape> shapesOf(const std::vector<geometry::StepPart>& parts)
{
    std::vector<TopoDS_Shape> shapes;
    shapes.reserve(parts.size());
    for (const geometry::StepPart& part : parts) {
        shapes.push_back(part.shape);
    }
    return shapes;
}

int componentCount(const std::vector<geometry::StepPart>& parts)
{
    std::set<std::vector<std::string>> nodes;
    for (const geometry::StepPart& part : parts) {
        for (std::size_t depth = 1; depth <= part.path.size(); ++depth) {
            nodes.emplace(part.path.begin(), part.path.begin() + static_cast<std::ptrdiff_t>(depth));
        }
    }
    return static_cast<int>(nodes.size());
}

double kilobytesOf(const std::filesystem::path& path)
{
    std::error_code error;
    std::uintmax_t bytes = 0;
    if (std::filesystem::is_directory(path, error)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path, error)) {
            if (entry.is_regular_file(error)) {
                bytes += entry.file_size(error);
            }
        }
    } else if (std::filesystem::exists(path, error)) {
        bytes = std::filesystem::file_size(path, error);
    }
    return static_cast<double>(bytes) / kBytesPerKilobyte;
}

std::vector<std::filesystem::path> stepFiles(const std::filesystem::path& input)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (std::filesystem::is_regular_file(input, error)) {
        files.push_back(input);
        return files;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(input, error)) {
        const QString extension = QString::fromStdU16String(entry.path().extension().u16string()).toLower();
        if (entry.is_regular_file(error) && (extension == ".step" || extension == ".stp")) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

std::string csvField(const QString& value)
{
    std::string text = value.toStdString();
    if (text.find_first_of(",\"\n\r") == std::string::npos) {
        return text;
    }
    std::string quoted = "\"";
    for (const char c : text) {
        quoted += c == '"' ? std::string("\"\"") : std::string(1, c);
    }
    return quoted + "\"";
}

QString fieldOf(const QJsonValue& value)
{
    if (value.isDouble()) {
        const double number = value.toDouble();
        return number == std::floor(number) && std::abs(number) < 1e15 ? QString::number(static_cast<qint64>(number))
                                                                       : QString::number(number, 'g', kVolumeDigits);
    }
    return value.toVariant().toString().simplified();
}

void writeRow(std::ofstream& csv, const QJsonObject& row)
{
    for (std::size_t i = 0; i < kColumns.size(); ++i) {
        csv << (i == 0 ? "" : ",") << csvField(fieldOf(row.value(QLatin1String(kColumns[i]))));
    }
    csv << '\n';
    csv.flush();
}

QJsonObject importInChild(const std::filesystem::path& step, const std::filesystem::path& work, int timeoutSeconds)
{
    std::error_code error;
    std::filesystem::remove_all(work, error);
    QProcess child;
    child.setProgram(QCoreApplication::applicationFilePath());
    child.setArguments({QLatin1String(kImportOneFlag), toQString(step), toQString(work)});
    child.setProcessChannelMode(QProcess::ForwardedChannels);
    QElapsedTimer timer;
    timer.start();
    child.start();
    const bool finished = child.waitForFinished(timeoutSeconds * kMillisecondsPerSecond);
    if (!finished) {
        child.kill();
        child.waitForFinished();
    }
    QJsonObject row;
    QFile file(toQString(work / kResultFile));
    if (finished && file.open(QIODevice::ReadOnly)) {
        row = QJsonDocument::fromJson(file.readAll()).object();
    }
    if (!finished) {
        row = {{"status", "timeout"}, {"error", QStringLiteral("No result after %1 s").arg(timeoutSeconds)}};
    } else if (row.isEmpty()) {
        row = {{"status", "crashed"}, {"error", QStringLiteral("Exit code %1").arg(child.exitCode())}};
    }
    row["file"] = toQString(step);
    row["total_ms"] = timer.elapsed();
    return row;
}

} // namespace

int runImportBatch(const std::filesystem::path& input, const std::filesystem::path& out, int timeoutSeconds)
{
    const std::vector<std::filesystem::path> files = stepFiles(input);
    if (files.empty()) {
        K137_ERROR(App, "Batch import found no STEP files in {}", toQString(input).toStdString());
        return 2;
    }
    std::error_code error;
    std::filesystem::create_directories(out, error);
    std::ofstream csv(out / "import.csv");
    for (std::size_t i = 0; i < kColumns.size(); ++i) {
        csv << (i == 0 ? "" : ",") << kColumns[i];
    }
    csv << '\n';
    int failures = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const QJsonObject row = importInChild(files[i], out / "work" / std::format("{:03}", i + 1), timeoutSeconds);
        writeRow(csv, row);
        if (row.value("status").toString() != QLatin1String("ok")) {
            ++failures;
        }
        K137_INFO(App, "Batch import {} of {}: {} {}", i + 1, files.size(),
                  row.value("status").toString().toStdString(), toQString(files[i]).toStdString());
    }
    return failures == 0 ? 0 : 1;
}

int runImportOne(const std::filesystem::path& step, const std::filesystem::path& work)
{
    std::error_code error;
    std::filesystem::create_directories(work, error);
    QJsonObject result;
    QString stage = QStringLiteral("read");
    try {
        QElapsedTimer timer;
        timer.start();
        std::vector<geometry::StepPart> parts = geometry::readStepParts(step);
        result["read_ms"] = timer.restart();
        result["parts"] = static_cast<int>(parts.size());
        result["components"] = componentCount(parts);
        result["colored"] = static_cast<int>(std::ranges::count_if(
            parts, [](const geometry::StepPart& part) { return part.color || !part.faceColors.empty(); }));

        stage = QStringLiteral("build");
        core::Document document;
        document.add(importFeature(step, std::move(parts)));
        const core::Model model = core::recompute(document);
        result["build_ms"] = timer.restart();
        result["failed"] = static_cast<int>(std::ranges::count_if(
            model.status, [](const auto& entry) { return entry.second.health == core::FeatureHealth::Failed; }));
        const Counts counts = countOf(model.shapes());
        result["measure_ms"] = timer.restart();
        result["solids"] = counts.solids;
        result["faces"] = counts.faces;
        result["volume"] = counts.volume;
        if (!counts.box.IsVoid()) {
            double low[3];
            double high[3];
            counts.box.Get(low[0], low[1], low[2], high[0], high[1], high[2]);
            for (int axis = 0; axis < 3; ++axis) {
                const std::string name(1, static_cast<char>('x' + axis));
                result[QString::fromStdString("min_" + name)] = low[axis];
                result[QString::fromStdString("max_" + name)] = high[axis];
            }
        }

        stage = QStringLiteral("save");
        const std::filesystem::path design = work / (std::string("design") + core::kDesignExtension);
        {
            core::ShapeStore shapes(core::shapeFolder(design));
            timer.restart();
            core::saveDesign(document, design, shapes);
            result["save_ms"] = timer.restart();
        }
        result["saved_kb"] = kilobytesOf(design) + kilobytesOf(core::shapeFolder(design));

        stage = QStringLiteral("reopen");
        {
            timer.restart();
            core::ShapeStore shapes(core::shapeFolder(design));
            const core::Model reopened = core::recompute(core::loadDesign(design, shapes));
            result["reopen_ms"] = timer.restart();
            result["reopen_solids"] = countOf(reopened.shapes()).solids;
        }

        stage = QStringLiteral("export");
        const std::filesystem::path exported = work / "export.step";
        timer.restart();
        geometry::writeStep(model.stepParts(), exported, std::string("Kalve 137 ") + KALVE137_VERSION);
        result["export_ms"] = timer.restart();
        result["export_kb"] = kilobytesOf(exported);

        stage = QStringLiteral("roundtrip");
        const std::vector<geometry::StepPart> readBack = geometry::readStepParts(exported);
        result["roundtrip_ms"] = timer.restart();
        const Counts back = countOf(shapesOf(readBack));
        result["roundtrip_solids"] = back.solids;
        result["roundtrip_faces"] = back.faces;
        result["roundtrip_volume"] = back.volume;
        const bool same =
            back.solids == counts.solids && back.faces == counts.faces &&
            std::abs(back.volume - counts.volume) <= kVolumeTolerance * std::max(1.0, std::abs(counts.volume));
        result["roundtrip"] = same ? "same" : "differs";
        result["status"] = "ok";
        result["stage"] = "";
    } catch (const std::exception& failure) {
        result["status"] = "failed";
        result["stage"] = stage;
        result["error"] = QString::fromUtf8(failure.what());
    }
    result["peak_mb"] = std::round(static_cast<double>(platform::peakMemory()) / kBytesPerMegabyte);

    QFile file(toQString(work / kResultFile));
    if (!file.open(QIODevice::WriteOnly)) {
        return 2;
    }
    file.write(QJsonDocument(result).toJson());
    return result.value("status").toString() == QLatin1String("ok") ? 0 : 1;
}

} // namespace kalve137::app
