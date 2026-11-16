// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include <QAbstractButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include <filesystem>
#include <system_error>

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

TEST_F(FilesTest, OpenStartsInTheLastUsedFolder)
{
    block(*ui);
    answerFile(scratch() / "part.k137");
    ui->trigger("Save");
    ui->trigger("New Design");

    QString start;
    answerDialog([&start](QWidget& widget) {
        if (const auto* dialog = qobject_cast<QFileDialog*>(&widget)) {
            start = dialog->directory().absolutePath();
        }
        widget.close();
    });
    ui->trigger("Open Design...");
    std::error_code ec;
    EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(start.toStdU16String()), scratch(), ec))
        << start.toStdString();
    EXPECT_TRUE(ui->document().features().empty());
}

TEST_F(FilesTest, VersionHistoryMarksRestoreAsPrimary)
{
    block(*ui);
    answerFile(scratch() / "part.k137");
    ui->trigger("Save");
    ui->trigger("Undo");
    ui->trigger("Save");

    bool gold = false;
    answerDialog([&gold](QWidget& widget) {
        for (QPushButton* button : widget.findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("Restore")) {
                gold = paintsAccent(*button);
            }
        }
        widget.close();
    });
    ui->trigger("Version History...");
    EXPECT_EQ(pendingAnswers(), 0U);
    EXPECT_TRUE(gold);
}

TEST_F(FilesTest, NewDesignAsksBeforeDroppingChanges)
{
    block(*ui);
    answerMessage(QMessageBox::Cancel);
    ui->trigger("New Design");
    EXPECT_EQ(ui->document().features().size(), 2U);

    QString discardText;
    answerDialog([&discardText](QWidget& widget) {
        auto* box = qobject_cast<QMessageBox*>(&widget);
        ASSERT_NE(box, nullptr);
        discardText = box->button(QMessageBox::Discard)->text();
        EXPECT_EQ(box->defaultButton(), box->button(QMessageBox::Save));
        EXPECT_TRUE(paintsAccent(*box->button(QMessageBox::Save)));
        EXPECT_FALSE(paintsAccent(*box->button(QMessageBox::Discard)));
        box->button(QMessageBox::Discard)->click();
    });
    ui->trigger("New Design");
    EXPECT_EQ(discardText, QStringLiteral("Don't Save"));
    EXPECT_TRUE(ui->document().features().empty());
    EXPECT_FALSE(ui->state()["dirty"]);
}

TEST_F(FilesTest, ClosingTheRecoveryPromptKeepsTheAutosave)
{
    block(*ui);
    const std::filesystem::path design = scratch() / "part.k137";
    answerFile(design);
    ui->trigger("Save");
    ui.reset();

    const std::filesystem::path folder =
        std::filesystem::path(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation).toStdU16String()) /
        "recovery";
    std::filesystem::create_directories(folder);
    const std::filesystem::path recovery = folder / "manual-recovery.k137";
    std::filesystem::copy_file(design, recovery);

    const auto closeAndCheck = [](QWidget& widget) {
        auto* box = qobject_cast<QMessageBox*>(&widget);
        ASSERT_NE(box, nullptr);
        EXPECT_EQ(box->windowTitle(), QStringLiteral("Recover Design"));
        EXPECT_NE(box->escapeButton(), nullptr) << "Close button is disabled without an escape button";
        EXPECT_EQ(box->defaultButton()->text(), QStringLiteral("Recover"));
        EXPECT_TRUE(paintsAccent(*box->defaultButton()));
        box->close();
    };
    answerDialog(closeAndCheck);
    ui = std::make_unique<UiDriver>();
    EXPECT_EQ(pendingAnswers(), 0U);
    EXPECT_TRUE(std::filesystem::exists(recovery)) << "Closing prompt must not delete autosave";
    EXPECT_TRUE(ui->document().features().empty());
    ui.reset();

    answerDialog([](QWidget& widget) {
        auto* box = qobject_cast<QMessageBox*>(&widget);
        ASSERT_NE(box, nullptr);
        for (QAbstractButton* button : box->buttons()) {
            if (button->text() == QStringLiteral("Discard")) {
                button->click();
                return;
            }
        }
        ADD_FAILURE() << "No Discard button";
        box->close();
    });
    ui = std::make_unique<UiDriver>();
    EXPECT_EQ(pendingAnswers(), 0U);
    EXPECT_FALSE(std::filesystem::exists(recovery));
}

TEST_F(FilesTest, OpenSampleOpensAnUntitledCopyOfTheDodecahedron)
{
    ui->trigger("Open Sample");
    const nlohmann::json state = ui->state();
    ASSERT_EQ(state["bodies"].size(), 1U) << state.dump(2);
    EXPECT_EQ(state["bodies"][0]["faces"], 12) << state.dump(2);
    EXPECT_FALSE(state["dirty"]);
    EXPECT_NE(state["title"].get<std::string>().find("Untitled"), std::string::npos) << state["title"];
    ui->save("sample");

    const std::filesystem::path copy = scratch() / "dodecahedron.k137";
    answerFile(copy);
    ui->trigger("Save");
    EXPECT_TRUE(std::filesystem::exists(copy));
    EXPECT_EQ(pendingAnswers(), 0U);
}

} // namespace
} // namespace kalve137::uitest
