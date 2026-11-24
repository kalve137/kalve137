// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/value_edit.h"
#include "app/panels/parameters_dialog.h"
#include "app/session/design_session.h"
#include "app/shell/command_search.h"
#include "app/shell/main_window.h"
#include "core/document.h"
#include "core/feature.h"
#include "core/parameter_table.h"
#include "render/annotations.h"
#include "render/rhi/rhi_view.h"
#include "scenario.h"
#include "sketch/sketch.h"
#include "ui_test.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QTreeWidget>

#include <gtest/gtest.h>

#include <gp_Pnt.hxx>

#include <cmath>
#include <string>
#include <variant>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr core::FeatureId kSketch = 1;
constexpr core::FeatureId kExtrude = 2;
constexpr int kName = 0;
constexpr int kExpression = 1;
constexpr int kValue = 2;
constexpr int kUsedBy = 3;
constexpr double kTolerance = 1e-3;

class ParametersTest : public UiTest {
protected:
    void SetUp() override
    {
        UiTest::SetUp();
        ui->session().reset(blockDocument(), {});
    }

    app::ParametersDialog* open()
    {
        ui->trigger("Parameters...");
        auto* dialog = ui->window().findChild<app::ParametersDialog*>();
        EXPECT_NE(dialog, nullptr);
        EXPECT_TRUE(dialog != nullptr && dialog->isVisible());
        return dialog;
    }

    QTableWidget* table() const { return ui->window().findChild<QTableWidget*>("parametersTable"); }

    QString cell(int row, int column) const
    {
        const QTableWidgetItem* item = table()->item(row, column);
        return item != nullptr ? item->text() : QString();
    }

    void commit(int row, int column, const QString& text)
    {
        table()->item(row, column)->setText(text);
        ui->settle();
    }

    void press(app::ParametersDialog& dialog, const QString& text)
    {
        for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == text) {
                button->click();
                ui->settle();
                return;
            }
        }
        ADD_FAILURE() << "No button " << text.toStdString();
    }

    void seed(const std::vector<core::Parameter>& parameters)
    {
        ui->session().edit(QStringLiteral("Seed"), [&](core::Document& document) {
            for (const core::Parameter& parameter : parameters) {
                EXPECT_FALSE(core::addParameter(document, parameter.name, parameter.expression));
            }
        });
        ui->settle();
    }

    void driveExtrude(const std::string& text)
    {
        ui->session().edit(QStringLiteral("Drive"), [&](core::Document& document) {
            EXPECT_FALSE(core::setFieldExpression(document, {kExtrude, "distance", 0}, text));
        });
        ui->settle();
    }

    std::vector<std::string> names() const
    {
        std::vector<std::string> found;
        for (const core::Parameter& parameter : ui->session().document().parameters()) {
            found.push_back(parameter.name);
        }
        return found;
    }
};

TEST_F(ParametersTest, CommandSearchOpensTheDialog)
{
    auto* search = ui->window().findChild<app::CommandSearch*>();
    ASSERT_NE(search, nullptr);
    search->popup();
    search->findChild<QLineEdit*>()->setText(QStringLiteral("Param"));
    auto* list = search->findChild<QTreeWidget*>();
    ASSERT_NE(list, nullptr);
    ASSERT_GT(list->topLevelItemCount(), 0);
    QTreeWidgetItem* first = list->topLevelItem(0);
    EXPECT_EQ(first->text(0), QStringLiteral("Parameters"));
    emit list->itemClicked(first, 0);
    ui->settle();
    auto* dialog = ui->window().findChild<app::ParametersDialog*>();
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(dialog->isVisible());
}

TEST_F(ParametersTest, AddsAParameterAndEditsItLiveWithOneUndoStep)
{
    app::ParametersDialog* dialog = open();
    ASSERT_NE(dialog, nullptr);
    press(*dialog, QStringLiteral("Add"));
    ASSERT_EQ(table()->rowCount(), 1);
    EXPECT_EQ(cell(0, kName), QStringLiteral("p1"));
    EXPECT_EQ(cell(0, kValue), QStringLiteral("10 mm"));

    commit(0, kName, QStringLiteral("h"));
    EXPECT_EQ(names(), std::vector<std::string>{"h"});
    driveExtrude("h");
    EXPECT_NEAR(volume(*ui), kBlockWidth * kBlockDepth * 10.0, kTolerance);
    EXPECT_EQ(cell(0, kUsedBy), QStringLiteral("Extrude1 distance"));

    commit(0, kExpression, QStringLiteral("12 + 3"));
    EXPECT_FALSE(dialog->errorText().isEmpty()) << "A plain number can't drive a length";
    EXPECT_EQ(cell(0, kExpression), QStringLiteral("10 mm"));

    commit(0, kExpression, QStringLiteral("12 mm + 3"));
    EXPECT_TRUE(dialog->errorText().isEmpty()) << dialog->errorText().toStdString();
    EXPECT_EQ(cell(0, kExpression), QStringLiteral("12 mm + 3 mm"));
    EXPECT_EQ(cell(0, kValue), QStringLiteral("15 mm"));
    EXPECT_NEAR(volume(*ui), kBlockVolume, kTolerance) << "The model follows the table";

    ui->session().undo();
    ui->settle();
    EXPECT_EQ(cell(0, kValue), QStringLiteral("10 mm")) << "One committed cell is one undo step";
    EXPECT_NEAR(volume(*ui), kBlockWidth * kBlockDepth * 10.0, kTolerance);
}

TEST_F(ParametersTest, RenameRewritesEveryExpression)
{
    seed({{"w", "40 mm"}, {"h", "w / 2"}});
    driveExtrude("h - 5 mm");
    app::ParametersDialog* dialog = open();
    ASSERT_NE(dialog, nullptr);
    commit(0, kName, QStringLiteral("width"));
    EXPECT_TRUE(dialog->errorText().isEmpty()) << dialog->errorText().toStdString();
    EXPECT_EQ(names(), (std::vector<std::string>{"width", "h"}));
    EXPECT_EQ(cell(1, kExpression), QStringLiteral("width / 2"));
    EXPECT_EQ(cell(0, kUsedBy), QStringLiteral("h"));
    commit(1, kName, QStringLiteral("height"));
    EXPECT_EQ(ui->session().document().find(kExtrude)->expressions.at("distance"), "height - 5 mm");

    commit(1, kName, QStringLiteral("mm"));
    EXPECT_FALSE(dialog->errorText().isEmpty());
    EXPECT_EQ(cell(1, kName), QStringLiteral("height")) << "A refused name leaves the row as it was";
}

TEST_F(ParametersTest, EditThatNamesALaterRowMovesItBelow)
{
    seed({{"w", "10 mm"}, {"h", "5 mm"}});
    app::ParametersDialog* dialog = open();
    ASSERT_NE(dialog, nullptr);
    commit(0, kExpression, QStringLiteral("h * 2"));
    EXPECT_EQ(names(), (std::vector<std::string>{"h", "w"}));
    EXPECT_EQ(cell(1, kValue), QStringLiteral("10 mm"));

    commit(0, kExpression, QStringLiteral("w + 1 mm"));
    EXPECT_EQ(dialog->errorText(), QStringLiteral("h and w name each other"));
    EXPECT_EQ(cell(0, kExpression), QStringLiteral("5 mm"));

    commit(1, kExpression, QStringLiteral("h * h + 5"));
    const QStringList lines = dialog->errorText().split('\n');
    ASSERT_EQ(lines.size(), 2) << dialog->errorText().toStdString();
    EXPECT_FALSE(lines[0].isEmpty());
    EXPECT_EQ(lines[1], QStringLiteral("Add a unit, like 5 mm2")) << "The hint sits under the message";
}

TEST_F(ParametersTest, DeleteRefusesAUsedParameter)
{
    seed({{"w", "40 mm"}, {"h", "15 mm"}, {"spare", "1 mm"}});
    driveExtrude("h");
    ui->session().edit(QStringLiteral("Drive"), [](core::Document& document) {
        auto& data = std::get<core::SketchFeature>(document.find(kSketch)->data);
        const sketch::Id bottom = data.sketch.curves().begin()->first;
        const sketch::Id dimension = data.sketch.addConstraint(sketch::ConstraintKind::Distance, {bottom}, kBlockWidth);
        EXPECT_FALSE(core::setFieldExpression(document, {kSketch, {}, dimension}, "h"));
    });
    app::ParametersDialog* dialog = open();
    ASSERT_NE(dialog, nullptr);

    table()->setCurrentCell(1, kName);
    press(*dialog, QStringLiteral("Delete"));
    EXPECT_EQ(names(), (std::vector<std::string>{"w", "h", "spare"}));
    EXPECT_EQ(dialog->errorText(), QStringLiteral("Used by Sketch1 distance 15 mm and Extrude1 distance"));

    table()->setCurrentCell(2, kName);
    press(*dialog, QStringLiteral("Delete"));
    EXPECT_EQ(names(), (std::vector<std::string>{"w", "h"}));
    EXPECT_TRUE(dialog->errorText().isEmpty());
    EXPECT_EQ(table()->rowCount(), 2);
}

class ValueNamesTest : public ParametersTest {
protected:
    void SetUp() override
    {
        ParametersTest::SetUp();
        seed({{"w", "10 mm"}});
        homeView(*ui);
    }

    app::ValueEdit* distanceField()
    {
        ui->doubleClick(ui->timelineChip(kExtrude));
        EXPECT_TRUE(ui->state()["featurePanel"]) << ui->state().dump(2);
        return qobject_cast<app::ValueEdit*>(ui->field("Distance"));
    }

    sketch::Id dimensionOnTheBottom()
    {
        sketch::Id dimension = 0;
        ui->session().edit(QStringLiteral("Dimension"), [&](core::Document& document) {
            document.remove(kExtrude);
            auto& data = std::get<core::SketchFeature>(document.find(kSketch)->data);
            dimension = data.sketch.addConstraint(sketch::ConstraintKind::Distance,
                                                  {data.sketch.curves().begin()->first}, kBlockWidth);
        });
        ui->doubleClick(ui->timelineChip(kSketch));
        EXPECT_EQ(ui->state()["sketch"], kSketch) << ui->state().dump(2);
        return dimension;
    }

    const sketch::Constraint* constraint(sketch::Id id) const
    {
        const sketch::Sketch* s = lastSketch(*ui);
        return s != nullptr ? s->constraint(id) : nullptr;
    }

    const core::ExtrudeFeature& extrudeData() const
    {
        return std::get<core::ExtrudeFeature>(ui->session().document().find(kExtrude)->data);
    }

    void setW(const std::string& expression)
    {
        EXPECT_FALSE(ui->session().tryEdit(QStringLiteral("Edit w"), [&](core::Document& document) {
            return core::setParameterExpression(document, "w", expression);
        }));
        ui->settle();
    }
};

const QLabel* visibleChild(const QWidget& parent, const QString& name)
{
    for (const QLabel* label : parent.findChildren<QLabel*>(name)) {
        if (label->isVisible()) {
            return label;
        }
    }
    return nullptr;
}

TEST_F(ValueNamesTest, ExtrudeFieldTakesANameAndShowsTheCanonicalText)
{
    app::ValueEdit* box = distanceField();
    ASSERT_NE(box, nullptr);
    ui->fill("Distance", "w+5", false);
    EXPECT_EQ(box->expression(), QStringLiteral("w + 5 mm"));
    EXPECT_EQ(box->text(), QStringLiteral("15 mm")) << "An idle driven field shows the value";
    const QLabel* mark = box->findChild<QLabel*>("fxMark");
    ASSERT_NE(mark, nullptr);
    EXPECT_TRUE(mark->isVisible());
    ui->save("driven distance");

    ui->click(box);
    EXPECT_EQ(box->text(), QStringLiteral("w + 5 mm")) << "A focused driven field shows its expression";
    EXPECT_FALSE(mark->isVisible());
    ui->key(Qt::Key_Return);
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    EXPECT_EQ(ui->session().document().find(kExtrude)->expressions.at("distance"), "w + 5 mm");
    EXPECT_NEAR(volume(*ui), kBlockWidth * kBlockDepth * 15.0, kTolerance);

    setW("20 mm");
    EXPECT_EQ(ui->session().document().find(kExtrude)->expressions.at("distance"), "w + 5 mm");
    EXPECT_NEAR(extrudeData().distance, 25.0, kTolerance);
    EXPECT_NEAR(volume(*ui), kBlockWidth * kBlockDepth * 25.0, kTolerance);
}

TEST_F(ValueNamesTest, ErrorShowsUnderTheField)
{
    app::ValueEdit* box = distanceField();
    ASSERT_NE(box, nullptr);
    ui->fill("Distance", "w + 5 deg");
    ASSERT_TRUE(ui->state()["featurePanel"]) << "A refused value keeps the panel open";
    ASSERT_NE(box->parentWidget(), nullptr);
    const QLabel* error = visibleChild(*box->parentWidget(), QStringLiteral("fieldError"));
    ASSERT_NE(error, nullptr);
    EXPECT_GE(error->geometry().top(), box->geometry().bottom());
    EXPECT_EQ(error->geometry().left(), box->geometry().left());
    EXPECT_FALSE(error->text().isEmpty());
    ui->save("error under the field");
    EXPECT_TRUE(ui->session().document().find(kExtrude)->expressions.empty());

    ui->enterValue(QStringLiteral("unknown * 2"));
    error = visibleChild(*box->parentWidget(), QStringLiteral("fieldError"));
    ASSERT_NE(error, nullptr);
    EXPECT_TRUE(error->text().contains(QStringLiteral("unknown"))) << error->text().toStdString();

    ui->enterValue(QStringLiteral("w"));
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    EXPECT_EQ(ui->session().document().find(kExtrude)->expressions.at("distance"), "w");
}

TEST_F(ValueNamesTest, APlainNumberClearsTheExpressionOfAField)
{
    driveExtrude("w * 2");
    app::ValueEdit* box = distanceField();
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->expression(), QStringLiteral("w * 2"));
    EXPECT_EQ(box->text(), QStringLiteral("20 mm"));
    ui->fill("Distance", "12");
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    EXPECT_TRUE(ui->session().document().find(kExtrude)->expressions.empty());
    EXPECT_NEAR(extrudeData().distance, 12.0, kTolerance);

    setW("30 mm");
    EXPECT_NEAR(extrudeData().distance, 12.0, kTolerance) << "The typed number stays after a parameter edit";
}

TEST_F(ValueNamesTest, AnEqualPlainNumberClearsTheExpressionOfAField)
{
    driveExtrude("w * 2");
    app::ValueEdit* box = distanceField();
    ASSERT_NE(box, nullptr);
    ui->fill("Distance", "20");
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    EXPECT_TRUE(ui->session().document().find(kExtrude)->expressions.empty());
    EXPECT_NEAR(extrudeData().distance, 20.0, kTolerance);

    setW("30 mm");
    EXPECT_NEAR(extrudeData().distance, 20.0, kTolerance) << "The typed number stays after a parameter edit";
}

TEST_F(ValueNamesTest, PatternCountTakesAName)
{
    seed({{"n", "4"}});
    ui->trigger("Pattern");
    ui->click(gp_Pnt(5, 3, kBlockHeight));
    auto* count = qobject_cast<app::ValueEdit*>(ui->field("Count"));
    ASSERT_NE(count, nullptr);

    ui->fill("Count", "2.5");
    ASSERT_TRUE(ui->state()["featurePanel"]) << "A refused count keeps the panel open";
    EXPECT_TRUE(count->property("invalid").toBool());
    ASSERT_NE(count->parentWidget(), nullptr);
    const QLabel* error = visibleChild(*count->parentWidget(), QStringLiteral("fieldError"));
    ASSERT_NE(error, nullptr);
    EXPECT_TRUE(error->text().contains(QStringLiteral("whole number"))) << error->text().toStdString();

    ui->fill("Count", "n", false);
    EXPECT_EQ(count->expression(), QStringLiteral("n"));
    ui->fill("Spacing", "50");
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    const core::Feature& pattern = ui->session().document().features().back();
    ASSERT_TRUE(std::holds_alternative<core::PatternFeature>(pattern.data));
    EXPECT_EQ(pattern.expressions.at("count"), "n");
    EXPECT_EQ(std::get<core::PatternFeature>(pattern.data).count, 4);

    EXPECT_FALSE(ui->session().tryEdit(QStringLiteral("Edit n"), [](core::Document& document) {
        return core::setParameterExpression(document, "n", "6");
    }));
    ui->settle();
    EXPECT_EQ(std::get<core::PatternFeature>(ui->session().document().features().back().data).count, 6);
}

TEST_F(ValueNamesTest, ADrivenDimensionLabelShowsTheMark)
{
    if (ui->window().newView() == nullptr) {
        GTEST_SKIP() << "Dimension labels carry the mark in the new view only";
    }
    const sketch::Id id = dimensionOnTheBottom();
    ASSERT_NE(id, 0);
    const auto labels = [this] {
        std::vector<std::u32string> texts;
        for (const render::LabelItem& label : ui->window().newView()->annotations().labels) {
            texts.push_back(label.text);
        }
        return texts;
    };
    editDimension(*ui, id, QStringLiteral("w*3"));
    ui->settle();
    EXPECT_EQ(labels(), (std::vector<std::u32string>{U"fx: 30"}));
    ui->save("driven dimension label");

    editDimension(*ui, id, QStringLiteral("30"));
    ui->settle();
    EXPECT_TRUE(constraint(id)->expression.empty());
    EXPECT_EQ(labels(), (std::vector<std::u32string>{U"30"}));
}

TEST_F(ValueNamesTest, DimensionEditorTakesANameAndAPlainNumberClearsIt)
{
    const sketch::Id id = dimensionOnTheBottom();
    ASSERT_NE(id, 0);
    editDimension(*ui, id, QStringLiteral("w*3"));
    ASSERT_NE(constraint(id), nullptr);
    EXPECT_EQ(constraint(id)->expression, "w * 3");
    EXPECT_NEAR(constraint(id)->value, 30.0, kTolerance);

    editDimension(*ui, id, QStringLiteral("w + 25"));
    EXPECT_EQ(constraint(id)->expression, "w + 25 mm") << "The stored text is canonical";
    EXPECT_NEAR(constraint(id)->value, 35.0, kTolerance);
    const nlohmann::json dimensions = ui->state()["dimensions"];
    ASSERT_EQ(dimensions.size(), 1U) << dimensions.dump(2);
    EXPECT_NEAR(dimensions[0]["value"].get<double>(), 35.0, kTolerance) << "The label shows the value";

    setW("15 mm");
    EXPECT_EQ(constraint(id)->expression, "w + 25 mm");
    EXPECT_NEAR(constraint(id)->value, 40.0, kTolerance);

    editDimension(*ui, id, QStringLiteral("w + 5 deg"));
    const QWidget* editor = ui->focused();
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->objectName(), "dimensionEditor");
    ASSERT_NE(editor->parentWidget(), nullptr);
    const QLabel* error = visibleChild(*editor->parentWidget(), QStringLiteral("fieldError"));
    ASSERT_NE(error, nullptr);
    EXPECT_TRUE(error->geometry().top() >= editor->geometry().bottom() ||
                error->geometry().bottom() <= editor->geometry().top())
        << "The error sits next to the editor, not over it";
    ui->save("dimension error");
    EXPECT_EQ(constraint(id)->expression, "w + 25 mm");

    ui->enterValue(QStringLiteral("50"));
    EXPECT_TRUE(constraint(id)->expression.empty());
    EXPECT_NEAR(constraint(id)->value, 50.0, kTolerance);
    setW("5 mm");
    EXPECT_NEAR(constraint(id)->value, 50.0, kTolerance) << "The typed number stays after a parameter edit";
}

TEST_F(ValueNamesTest, TheCompleterOffersNamesAndInsertsTheChoice)
{
    seed({{"wall", "2 mm"}});
    app::ValueEdit* box = distanceField();
    ASSERT_NE(box, nullptr);
    ui->click(box);
    box->selectAll();
    ui->type(QStringLiteral("wa"));
    QAbstractItemView* popup = nullptr;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("nameCompleter") && widget->isVisible()) {
            popup = qobject_cast<QAbstractItemView*>(widget);
        }
    }
    ASSERT_NE(popup, nullptr) << "Typing the start of a name opens the completer";
    EXPECT_EQ(popup->objectName(), QStringLiteral("nameCompleter"));
    ASSERT_EQ(popup->model()->rowCount(), 1);
    EXPECT_EQ(popup->model()->index(0, 0).data().toString(), QStringLiteral("wall"));
    ui->save("name completer");

    const QModelIndex wall = popup->model()->index(0, 0);
    QTest::mouseClick(popup->viewport(), Qt::LeftButton, {}, popup->visualRect(wall).center());
    ui->settle();
    EXPECT_EQ(box->text(), QStringLiteral("wall"));
    EXPECT_FALSE(popup->isVisible());
    ASSERT_TRUE(ui->state()["featurePanel"]) << "Picking a name doesn't accept the panel";

    ui->key(Qt::Key_Return);
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    EXPECT_EQ(ui->session().document().find(kExtrude)->expressions.at("distance"), "wall");
    EXPECT_NEAR(extrudeData().distance, 2.0, kTolerance);
}

TEST_F(ValueNamesTest, LeavingADrivenFieldWithoutTypingShowsTheValue)
{
    driveExtrude("w * 2");
    app::ValueEdit* box = distanceField();
    ASSERT_NE(box, nullptr);
    ui->click(box);
    EXPECT_EQ(box->text(), QStringLiteral("w * 2"));
    ui->key(Qt::Key_Tab);
    ASSERT_FALSE(box->hasFocus());
    EXPECT_EQ(box->text(), QStringLiteral("20 mm")) << "Focus out shows the value even without an edit";
    const QLabel* mark = box->findChild<QLabel*>("fxMark");
    ASSERT_NE(mark, nullptr);
    EXPECT_TRUE(mark->isVisible());
    EXPECT_EQ(box->expression(), QStringLiteral("w * 2"));
}

TEST_F(ValueNamesTest, AFailedCommitOnFocusOutKeepsTheLastGoodValue)
{
    app::ValueEdit* box = distanceField();
    ASSERT_NE(box, nullptr);
    const double before = extrudeData().distance;
    const QString shown = box->text();
    ui->click(box);
    box->selectAll();
    ui->type(QStringLiteral("15"));
    EXPECT_NEAR(box->value(), 15.0, kTolerance) << "Typing previews the value";
    ui->type(QStringLiteral(" + q"));
    ui->key(Qt::Key_Return);
    ASSERT_TRUE(ui->state()["featurePanel"]);
    EXPECT_EQ(box->text(), QStringLiteral("15 + q")) << "A refused Return leaves the text to fix";
    EXPECT_NEAR(box->value(), std::abs(before), kTolerance);
    ui->key(Qt::Key_Tab);
    ASSERT_FALSE(box->hasFocus());
    EXPECT_NEAR(box->value(), std::abs(before), kTolerance) << "The refused text gives back the last good value";
    EXPECT_EQ(box->text(), shown);
    ASSERT_NE(box->parentWidget(), nullptr);
    const QLabel* error = visibleChild(*box->parentWidget(), QStringLiteral("fieldError"));
    ASSERT_NE(error, nullptr) << "The error stays after focus out";
    EXPECT_TRUE(error->text().contains(QStringLiteral("q"))) << error->text().toStdString();
    ui->save("refused on focus out");

    ui->click(box);
    ui->key(Qt::Key_Return);
    ASSERT_FALSE(ui->state()["featurePanel"]) << ui->state().dump(2);
    EXPECT_NEAR(extrudeData().distance, before, kTolerance);
    EXPECT_TRUE(ui->session().document().find(kExtrude)->expressions.empty());
}

TEST_F(ParametersTest, RetypingTheSameCanonicalTextPushesNoUndoStep)
{
    seed({{"w", "10 mm"}, {"h", "w * 2"}});
    app::ParametersDialog* dialog = open();
    ASSERT_NE(dialog, nullptr);
    commit(1, kExpression, QStringLiteral("w*2"));
    EXPECT_TRUE(dialog->errorText().isEmpty()) << dialog->errorText().toStdString();
    EXPECT_EQ(cell(1, kExpression), QStringLiteral("w * 2"));
    ui->session().undo();
    ui->settle();
    EXPECT_TRUE(names().empty()) << "Undo takes back the seed, not an edit that changed nothing";
}

} // namespace
} // namespace kalve137::uitest
