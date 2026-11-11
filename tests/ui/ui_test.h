// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_driver.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace kalve137::uitest {

class UiTest : public testing::Test {
protected:
    void SetUp() override
    {
        resetSettings();
        takeProblems();
        pendingAnswers();
        ui = std::make_unique<UiDriver>();
    }

    void TearDown() override
    {
        ui.reset();
        for (const std::string& problem : takeProblems()) {
            ADD_FAILURE() << problem;
        }
        if (const std::size_t left = pendingAnswers(); left > 0) {
            ADD_FAILURE() << left << " dialog answers were never used";
        }
        resetSettings();
        if (!m_scratch.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(m_scratch, ignored);
        }
    }

    std::filesystem::path scratch()
    {
        if (m_scratch.empty()) {
            const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
            m_scratch = std::filesystem::temp_directory_path() / "kalve137-uitests" /
                        (std::string(test->test_suite_name()) + "." + test->name());
            std::error_code ignored;
            std::filesystem::remove_all(m_scratch, ignored);
            std::filesystem::create_directories(m_scratch);
        }
        return m_scratch;
    }

    std::unique_ptr<UiDriver> ui;

private:
    std::filesystem::path m_scratch;
};

} // namespace kalve137::uitest
