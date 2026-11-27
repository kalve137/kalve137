// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scenario.h"
#include "ui_test.h"

#include "app/shell/main_window.h"
#include "app/theme/theme.h"
#include "viewer/occt_view_widget.h"

#include <QAbstractButton>
#include <QGuiApplication>
#include <QPointF>
#include <QScreen>
#include <QScrollArea>
#include <QString>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <string>

namespace kalve137::uitest {
namespace {

constexpr QSize kFloor{1280, 640};
constexpr int kVisibleItems = 5;
constexpr double kBoxPadding = 20.0;
constexpr int kZoomOutNotches = -4;

class ScreenTest : public UiTest {
protected:
    void SetUp() override
    {
        UiTest::SetUp();
        ui->window().resize(kFloor);
        ui->settle(50);
    }
};

QToolBar* bar(UiDriver& ui, const char* name)
{
    return ui.window().findChild<QToolBar*>(name);
}

bool overflows(const QToolBar& toolBar)
{
    for (const QToolButton* button : toolBar.findChildren<QToolButton*>("qt_toolbar_ext_button")) {
        if (button->isVisible()) {
            return true;
        }
    }
    return false;
}

bool labeled(const QToolBar& toolBar)
{
    for (const QToolButton* button : toolBar.findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) {
        if (button->isVisible() && button->toolButtonStyle() == Qt::ToolButtonTextUnderIcon &&
            button->objectName() != "finishSketch") {
            return true;
        }
    }
    return false;
}

QRect inWindow(UiDriver& ui, const QWidget& widget)
{
    return {widget.mapTo(&ui.window(), QPoint()), widget.size()};
}

void saveInBothThemes(UiDriver& ui, const std::string& name)
{
    ui.save(name + "-dark");
    app::theme::setScheme(app::theme::Scheme::Light);
    ui.settle(50);
    ui.save(name + "-light");
    app::theme::setScheme(app::theme::Scheme::Dark);
    ui.settle(50);
}

TEST_F(ScreenTest, FloorFitsTheWindowAndBothToolbars)
{
    EXPECT_EQ(ui->window().size(), kFloor);
    QToolBar* create = bar(*ui, "createBar");
    ASSERT_NE(create, nullptr);
    EXPECT_FALSE(overflows(*create));
    EXPECT_TRUE(labeled(*create)) << "Create bar dropped its labels although they fit";
    saveInBothThemes(*ui, "floor");

    ui->trigger("Create Sketch");
    ui->click(ui->originPlane(core::OriginPlane::XY));
    ui->settle(700);
    QToolBar* sketch = bar(*ui, "sketchBar");
    ASSERT_NE(sketch, nullptr);
    EXPECT_FALSE(overflows(*sketch)) << "Sketch tools spill into the overflow menu, " << sketch->sizeHint().width()
                                     << " wide in " << sketch->width() << ", labeled " << labeled(*sketch);
    const auto* finish = ui->window().findChild<QToolButton*>("finishSketch");
    ASSERT_NE(finish, nullptr);
    EXPECT_TRUE(finish->isVisible());
    EXPECT_EQ(finish->toolButtonStyle(), Qt::ToolButtonTextUnderIcon);
    EXPECT_TRUE(ui->window().rect().contains(inWindow(*ui, *finish)));
    saveInBothThemes(*ui, "floor-sketch");

    ui->window().resize(1920, 1000);
    ui->settle(50);
    EXPECT_FALSE(overflows(*sketch));
    EXPECT_TRUE(labeled(*sketch)) << "Sketch bar kept icons only at full width";
}

TEST_F(ScreenTest, DialogsFitTheFloorAndTheScreen)
{
    const QSize room = QGuiApplication::primaryScreen()->availableGeometry().size();
    for (const QString& action :
         {QStringLiteral("Preferences..."), QStringLiteral("Keyboard Shortcuts"), QStringLiteral("About Kalve 137"),
          QStringLiteral("Report a Problem..."), QStringLiteral("Properties...")}) {
        bool shown = false;
        answerDialog([&shown, action, room](QWidget& widget) {
            shown = true;
            const QSize least = widget.minimumSizeHint();
            EXPECT_LE(least.width(), kFloor.width()) << action.toStdString();
            EXPECT_LE(least.height(), kFloor.height()) << action.toStdString();
            EXPECT_LE(widget.width(), room.width()) << action.toStdString();
            EXPECT_LE(widget.height(), room.height()) << action.toStdString();
            widget.close();
        });
        ui->trigger(action);
        EXPECT_TRUE(shown) << action.toStdString();
    }
}

TEST_F(ScreenTest, LongSelectionKeepsPanelButtonsInView)
{
    block(*ui);
    ui->wheel(ui->centerOf(ui->canvas()), kZoomOutNotches);
    ui->settle(50);
    ui->trigger("Fillet");
    ui->settle(50);
    QPointF low = ui->at(gp_Pnt(0, 0, 0));
    QPointF high = low;
    for (const double x : {-kBlockWidth / 2, kBlockWidth / 2}) {
        for (const double y : {-kBlockDepth / 2, kBlockDepth / 2}) {
            for (const double z : {0.0, kBlockHeight}) {
                const QPointF at = ui->at(gp_Pnt(x, y, z));
                low = {std::min(low.x(), at.x()), std::min(low.y(), at.y())};
                high = {std::max(high.x(), at.x()), std::max(high.y(), at.y())};
            }
        }
    }
    const QPointF padding(kBoxPadding, kBoxPadding);
    ui->drag(low - padding, high + padding);
    ui->settle(100);
    ASSERT_TRUE(ui->selectionText().startsWith("9 Edges")) << ui->selectionText().toStdString();

    const auto* list = ui->window().findChild<QScrollArea*>("selectionItems");
    ASSERT_NE(list, nullptr);
    const auto* row = list->widget()->findChild<QWidget*>();
    ASSERT_NE(row, nullptr);
    EXPECT_LE(list->height(), kVisibleItems * row->sizeHint().height());
    const QAbstractButton* ok = ui->button("OK");
    ASSERT_NE(ok, nullptr);
    EXPECT_TRUE(inWindow(*ui, *ui->canvas()).contains(inWindow(*ui, *ok)));
    ui->save("fillet-nine-edges");
    ui->key(Qt::Key_Escape);
}

} // namespace
} // namespace kalve137::uitest
