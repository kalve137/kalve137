// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace kalve137::uitest {
namespace {

constexpr double kTolerance = 1e-3;

using FilesTest = UiTest;

TEST_F(FilesTest, SavesReopensExportsAndImports)
{
    block(*ui);
    const std::filesystem::path design = scratch() / "part.k137";
    answerFile(design);
    ui->trigger("Save");
    ASSERT_TRUE(std::filesystem::exists(design)) << ui->state().dump(2);
    EXPECT_FALSE(ui->state()["dirty"]);
    EXPECT_NE(ui->state()["title"].get<std::string>().find("part"), std::string::npos);

    ui->trigger("New Design");
    EXPECT_TRUE(ui->document().features().empty());

    answerFile(design);
    ui->trigger("Open Design...");
    ASSERT_EQ(ui->document().features().size(), 2U) << ui->state().dump(2);
    EXPECT_NEAR(volume(*ui), kBlockVolume, kTolerance);
    ui->save("reopened");

    const std::filesystem::path step = scratch() / "part.step";
    answerFile(step);
    ui->trigger("Export STEP...");
    ASSERT_TRUE(std::filesystem::exists(step));
    EXPECT_GT(std::filesystem::file_size(step), 0U);

    const std::filesystem::path stl = scratch() / "part.stl";
    answerFile(stl);
    ui->trigger("Export STL...");
    ASSERT_TRUE(std::filesystem::exists(stl));
    EXPECT_GT(std::filesystem::file_size(stl), 0U);

    ui->trigger("New Design");
    answerFile(step);
    ui->trigger("Import STEP...");
    ui->settle(500);
    ASSERT_EQ(ui->state()["bodies"].size(), 1U) << ui->state().dump(2);
    EXPECT_NEAR(volume(*ui), kBlockVolume, kTolerance);
    ui->save("imported");
}

TEST_F(FilesTest, NewDesignAsksBeforeDroppingChanges)
{
    block(*ui);
    answerMessage(QMessageBox::Cancel);
    ui->trigger("New Design");
    EXPECT_EQ(ui->document().features().size(), 2U);

    answerMessage(QMessageBox::Discard);
    ui->trigger("New Design");
    EXPECT_TRUE(ui->document().features().empty());
    EXPECT_FALSE(ui->state()["dirty"]);
}

} // namespace
} // namespace kalve137::uitest
