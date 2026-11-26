// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include "app/feature/measure_panel.h"
#include "app/shell/main_window.h"

#include <QLabel>
#include <QString>

#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

namespace kalve137::uitest {
namespace {

const gp_Pnt kTopFace(5, 3, kBlockHeight);
const gp_Pnt kBottomCorner(-kBlockWidth / 2, -kBlockDepth / 2, 0);

QString labelText(const app::MeasurePanel& panel, bool table)
{
    for (const QLabel* label : panel.findChildren<QLabel*>()) {
        const bool isTable = label->text().startsWith(QStringLiteral("<table>"));
        if (label->objectName() == QStringLiteral("panelHint") && !table) {
            return label->text();
        }
        if (isTable && table) {
            return label->text();
        }
    }
    return {};
}

using MeasureTest = UiTest;

TEST_F(MeasureTest, ShowsDistanceBetweenTwoPicks)
{
    block(*ui);
    homeView(*ui);
    auto* panel = ui->window().findChild<app::MeasurePanel*>();
    ASSERT_NE(panel, nullptr);
    ui->trigger("Measure");
    ASSERT_TRUE(panel->isActive());

    ui->click(kTopFace);
    ui->click(kBottomCorner);
    ui->save("measure");
    EXPECT_EQ(labelText(*panel, false), QStringLiteral("Face + Vertex"));
    const QString table = labelText(*panel, true);
    EXPECT_TRUE(table.contains(QStringLiteral("Distance"))) << table.toStdString();
    EXPECT_TRUE(table.contains(QStringLiteral("15.000 mm"))) << table.toStdString();

    ui->pressButton("Close");
    EXPECT_FALSE(panel->isActive());
}

} // namespace
} // namespace kalve137::uitest
