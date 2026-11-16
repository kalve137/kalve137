// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include "viewer/occt_view_widget.h"

#include <QLineEdit>
#include <QRectF>

#include <gtest/gtest.h>

namespace kalve137::uitest {
namespace {

using LineTest = UiTest;

constexpr int kSamples = 64;

const QLineEdit* angleBox(UiDriver& ui)
{
    for (const QLineEdit* edit : ui.view().findChildren<QLineEdit*>("sketchInput")) {
        if (edit->isVisible() && edit->text().endsWith(QChar(0x00B0))) {
            return edit;
        }
    }
    return nullptr;
}

void expectClear(UiDriver& ui, const gp_Pnt& from, const gp_Pnt& to)
{
    const QLineEdit* box = angleBox(ui);
    ASSERT_NE(box, nullptr) << "Angle box is showing";
    ASSERT_NE(box->text(), QString::fromUtf8("0.0\u00B0")) << "Line snapped flat";
    const QRectF rect(box->geometry());
    const QPointF a = ui.at(from);
    const QPointF b = ui.at(to);
    for (int i = 0; i <= kSamples; ++i) {
        const QPointF p = a + (b - a) * (static_cast<double>(i) / kSamples);
        ASSERT_FALSE(rect.contains(p)) << "Angle box covers line at " << p.x() << ", " << p.y();
    }
}

TEST_F(LineTest, AngleBoxStaysOffAShallowLine)
{
    ui->trigger("Create Sketch");
    ui->click(ui->originPlane(core::OriginPlane::XY));
    ASSERT_NE(ui->state()["sketch"], 0) << ui->state().dump(2);
    ui->trigger("Line");

    const gp_Pnt start(-60, 0, 0);
    const gp_Pnt joint(60, 15, 0);
    const gp_Pnt end(-60, 25, 0);
    ui->click(start);
    ui->move(ui->at(joint));
    ui->save("shallow first segment");
    expectClear(*ui, start, joint);

    ui->click(joint);
    ui->move(ui->at(end));
    ui->save("sharp fold back");
    expectClear(*ui, joint, end);
    expectClear(*ui, start, joint);
}

} // namespace
} // namespace kalve137::uitest
