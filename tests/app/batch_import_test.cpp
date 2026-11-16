// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/step_import.h"
#include "app/shell/batch_import.h"

#include "geometry/exchange.h"
#include "geometry/shapes.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <gp_Pnt.hxx>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

using kalve137::geometry::makeBox;
using kalve137::geometry::makeCylinder;
using kalve137::geometry::StepPart;

QJsonObject readResult(const std::filesystem::path& work)
{
    QFile file(QString::fromStdU16String((work / "result.json").u16string()));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

TEST(StepImport, NamesUnnamedPartsAfterTheFile)
{
    const auto single = kalve137::app::importFeature("bracket.step", {{"", makeBox(1.0, 1.0, 1.0), {}, {}, {}}});
    EXPECT_EQ(single.source, "bracket.step");
    EXPECT_EQ(single.parts.at(0).name, "bracket");

    const auto several = kalve137::app::importFeature(
        "rig.stp", {{"Base", makeBox(1.0, 1.0, 1.0), {}, {}, {}}, {"", makeBox(2.0, 2.0, 2.0), {}, {}, {}}});
    EXPECT_EQ(several.parts.at(0).name, "Base");
    EXPECT_EQ(several.parts.at(1).name, "rig 2");
}

TEST(BatchImport, MeasuresOneFileEndToEnd)
{
    const auto folder = std::filesystem::temp_directory_path() / "kalve137-test-batch-import";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::vector<std::string> rig{"Rig"};
    kalve137::geometry::writeStep(
        {{"Base", makeBox(10.0, 10.0, 10.0), kalve137::geometry::Color{1.0, 0.0, 0.0}, {}, rig},
         {"Pin", makeCylinder(gp_Pnt(50.0, 0.0, 0.0), 5.0, 10.0), std::nullopt, {}, rig}},
        folder / "rig.step");

    EXPECT_EQ(kalve137::app::runImportOne(folder / "rig.step", folder / "work"), 0);
    const QJsonObject result = readResult(folder / "work");
    EXPECT_EQ(result.value("status").toString(), "ok");
    EXPECT_EQ(result.value("parts").toInt(), 2);
    EXPECT_EQ(result.value("solids").toInt(), 2);
    EXPECT_EQ(result.value("faces").toInt(), 9);
    EXPECT_EQ(result.value("components").toInt(), 1);
    EXPECT_EQ(result.value("colored").toInt(), 1);
    EXPECT_EQ(result.value("failed").toInt(), 0);
    EXPECT_EQ(result.value("reopen_solids").toInt(), 2);
    EXPECT_EQ(result.value("roundtrip").toString(), "same");
    EXPECT_GT(result.value("saved_kb").toDouble(), 0.0);
    EXPECT_NEAR(result.value("min_x").toDouble(), 0.0, 1e-6);
    EXPECT_NEAR(result.value("max_x").toDouble(), 55.0, 1e-6);
    EXPECT_NEAR(result.value("max_z").toDouble(), 10.0, 1e-6);
    EXPECT_GT(result.value("peak_mb").toDouble(), 0.0);
    std::filesystem::remove_all(folder);
}

TEST(BatchImport, RecordsTheStageThatFailed)
{
    const auto folder = std::filesystem::temp_directory_path() / "kalve137-test-batch-import-broken";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    {
        std::ofstream broken(folder / "broken.step");
        broken << "not a step file\n";
    }

    EXPECT_EQ(kalve137::app::runImportOne(folder / "broken.step", folder / "work"), 1);
    const QJsonObject result = readResult(folder / "work");
    EXPECT_EQ(result.value("status").toString(), "failed");
    EXPECT_EQ(result.value("stage").toString(), "read");
    EXPECT_FALSE(result.value("error").toString().isEmpty());
    std::filesystem::remove_all(folder);
}

} // namespace
