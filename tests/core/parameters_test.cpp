// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"
#include "core/parameter_table.h"
#include "core/recompute.h"
#include "core/serialization.h"
#include "core/undo_stack.h"
#include "geometry/shapes.h"

#include <gtest/gtest.h>

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

using namespace kalve137;
using namespace kalve137::core;

namespace {

constexpr double kTolerance = 1e-9;

struct Rectangle {
    FeatureId sketch = 0;
    sketch::Id width = 0;
    sketch::Id height = 0;
    FeatureId extrude = 0;
};

Rectangle addBlock(Document& doc, double width, double height, double depth)
{
    SketchFeature data;
    sketch::Sketch& s = data.sketch;
    const sketch::Id a = s.addPoint({0, 0}, true);
    const sketch::Id b = s.addPoint({width, 0});
    const sketch::Id c = s.addPoint({width, height});
    const sketch::Id d = s.addPoint({0, height});
    const sketch::Id bottom = s.addLine(a, b);
    const sketch::Id right = s.addLine(b, c);
    const sketch::Id top = s.addLine(c, d);
    const sketch::Id left = s.addLine(d, a);
    s.addConstraint(sketch::ConstraintKind::Horizontal, {bottom});
    s.addConstraint(sketch::ConstraintKind::Horizontal, {top});
    s.addConstraint(sketch::ConstraintKind::Vertical, {right});
    s.addConstraint(sketch::ConstraintKind::Vertical, {left});
    Rectangle result;
    result.width = s.addConstraint(sketch::ConstraintKind::HorizontalDistance, {a, b}, width);
    result.height = s.addConstraint(sketch::ConstraintKind::VerticalDistance, {b, c}, height);
    result.sketch = doc.add(std::move(data));
    ExtrudeFeature extrude;
    extrude.profiles = {result.sketch, {{1.0, 1.0}}};
    extrude.distance = depth;
    result.extrude = doc.add(std::move(extrude));
    return result;
}

const sketch::Constraint& constraintOf(const Document& doc, FeatureId sketch, sketch::Id id)
{
    return *std::get<SketchFeature>(doc.find(sketch)->data).sketch.constraint(id);
}

template <typename T>
const T& dataOf(const Document& doc, FeatureId id)
{
    return std::get<T>(doc.find(id)->data);
}

std::vector<std::string> namesOf(const Document& doc)
{
    std::vector<std::string> names;
    for (const Parameter& parameter : doc.parameters()) {
        names.push_back(parameter.name);
    }
    return names;
}

Bnd_Box boundsOf(const Model& model)
{
    Bnd_Box box;
    for (const Body& body : model.bodies) {
        BRepBndLib::Add(body.shape, box);
    }
    return box;
}

void expectSize(const Model& model, double x, double y, double z)
{
    double x0 = 0.0;
    double y0 = 0.0;
    double z0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double z1 = 0.0;
    boundsOf(model).Get(x0, y0, z0, x1, y1, z1);
    EXPECT_NEAR(x1 - x0, x, 1e-4);
    EXPECT_NEAR(y1 - y0, y, 1e-4);
    EXPECT_NEAR(z1 - z0, z, 1e-4);
}

Document drivenBlock(Rectangle& block)
{
    Document doc;
    block = addBlock(doc, 80.0, 20.0, 40.0);
    EXPECT_FALSE(addParameter(doc, "w", "40 mm"));
    EXPECT_FALSE(setFieldExpression(doc, {block.sketch, {}, block.width}, "w * 2"));
    EXPECT_FALSE(setFieldExpression(doc, {block.extrude, "distance"}, "w"));
    return doc;
}

} // namespace

TEST(Parameters, OneParameterDrivesADimensionAndAnExtrude)
{
    Rectangle block;
    Document doc = drivenBlock(block);
    Model model = recompute(doc);
    ASSERT_EQ(model.status.at(block.extrude).health, FeatureHealth::Ok) << model.status.at(block.extrude).message;
    expectSize(model, 80.0, 20.0, 40.0);

    ASSERT_FALSE(setParameterExpression(doc, "w", "50 mm"));
    EXPECT_NEAR(constraintOf(doc, block.sketch, block.width).value, 100.0, kTolerance);
    EXPECT_NEAR(dataOf<ExtrudeFeature>(doc, block.extrude).distance, 50.0, kTolerance);
    model = recompute(doc);
    ASSERT_EQ(model.status.at(block.extrude).health, FeatureHealth::Ok) << model.status.at(block.extrude).message;
    expectSize(model, 100.0, 20.0, 50.0);
    EXPECT_NEAR(geometry::volume(model.bodies.front().shape), 100.0 * 20.0 * 50.0, 1e-3);
}

TEST(Parameters, EvaluateTopToBottom)
{
    Document doc;
    ASSERT_FALSE(addParameter(doc, "w", "40 mm"));
    ASSERT_FALSE(addParameter(doc, "h", "w / 2"));
    ASSERT_FALSE(addParameter(doc, "area", "w * h"));
    const std::optional<ParameterValues> values = parameterValues(doc);
    ASSERT_TRUE(values);
    EXPECT_NEAR(values->at("h").value, 20.0, kTolerance);
    EXPECT_NEAR(values->at("area").value, 800.0, kTolerance);
    EXPECT_EQ(values->at("area").dim, (Dim{2, 0}));

    const std::optional<ExpressionError> later = addParameter(doc, "d", "e * 2");
    ASSERT_TRUE(later);
    EXPECT_EQ(later->code, "unknown-name");
    EXPECT_EQ(namesOf(doc), (std::vector<std::string>{"w", "h", "area"}));

    Document unordered;
    unordered.setParameters({{"h", "w / 2"}, {"w", "40 mm"}});
    const std::optional<ExpressionError> error = applyParameters(unordered);
    ASSERT_TRUE(error);
    EXPECT_EQ(error->hint, "'w' is defined further down. Move it above this line");
}

TEST(Parameters, AnEditMovesTheRowBelowWhatItNames)
{
    Document doc;
    ASSERT_FALSE(addParameter(doc, "w", "10 mm"));
    ASSERT_FALSE(addParameter(doc, "t", "2 mm"));
    ASSERT_FALSE(addParameter(doc, "h", "30 mm"));
    ASSERT_FALSE(setParameterExpression(doc, "w", "h / 2"));
    EXPECT_EQ(namesOf(doc), (std::vector<std::string>{"t", "h", "w"}));
    EXPECT_NEAR(parameterValues(doc)->at("w").value, 15.0, kTolerance);
}

TEST(Parameters, CyclesAreRefusedAndChangeNothing)
{
    Document doc;
    ASSERT_FALSE(addParameter(doc, "w", "10 mm"));
    ASSERT_FALSE(addParameter(doc, "h", "w * 2"));
    ASSERT_FALSE(addParameter(doc, "d", "h + 1 mm"));

    const std::optional<ExpressionError> pair = setParameterExpression(doc, "w", "d / 2");
    ASSERT_TRUE(pair);
    EXPECT_EQ(pair->code, "cycle");
    EXPECT_EQ(pair->message, "w and d name each other");

    const std::optional<ExpressionError> self = setParameterExpression(doc, "w", "w + 1 mm");
    ASSERT_TRUE(self);
    EXPECT_EQ(self->message, "w names itself");

    EXPECT_EQ(namesOf(doc), (std::vector<std::string>{"w", "h", "d"}));
    EXPECT_EQ(doc.parameters().front().expression, "10 mm");
}

TEST(Parameters, InputIsForgivingAndTheStoredFormIsCanonical)
{
    Rectangle block;
    Document doc = drivenBlock(block);
    ASSERT_FALSE(setFieldExpression(doc, {block.extrude, "distance"}, "w+5"));
    EXPECT_EQ(doc.find(block.extrude)->expressions.at("distance"), "w + 5 mm");
    EXPECT_NEAR(dataOf<ExtrudeFeature>(doc, block.extrude).distance, 45.0, kTolerance);
    ASSERT_FALSE(setParameterExpression(doc, "w", "4,5 cm"));
    EXPECT_EQ(doc.parameters().front().expression, "4.5 cm");
    EXPECT_NEAR(dataOf<ExtrudeFeature>(doc, block.extrude).distance, 50.0, kTolerance);
}

TEST(Parameters, ReconcileDropsExpressionsOfValuesWrittenAnotherWay)
{
    Rectangle block;
    const Document before = drivenBlock(block);

    Document flipped = before;
    std::get<ExtrudeFeature>(flipped.find(block.extrude)->data).distance = -40.0;
    reconcileExpressions(before, flipped);
    EXPECT_EQ(flipped.find(block.extrude)->expressions.size(), 1U) << "A flip keeps the expression";

    Document dragged = before;
    std::get<ExtrudeFeature>(dragged.find(block.extrude)->data).distance = 25.0;
    std::get<SketchFeature>(dragged.find(block.sketch)->data).sketch.constraint(block.width)->value = 60.0;
    reconcileExpressions(before, dragged);
    EXPECT_TRUE(dragged.find(block.extrude)->expressions.empty());
    EXPECT_TRUE(constraintOf(dragged, block.sketch, block.width).expression.empty());
    ASSERT_FALSE(applyParameters(dragged));
    EXPECT_NEAR(dataOf<ExtrudeFeature>(dragged, block.extrude).distance, 25.0, kTolerance);

    Document stamped = before;
    ASSERT_FALSE(setParameterExpression(stamped, "w", "50 mm"));
    reconcileExpressions(before, stamped);
    EXPECT_EQ(stamped.find(block.extrude)->expressions.at("distance"), "w") << "A stamp is not an override";
    EXPECT_EQ(constraintOf(stamped, block.sketch, block.width).expression, "w * 2");

    Document table = before;
    std::vector<Parameter> parameters = table.parameters();
    parameters.front().expression = "50 mm";
    table.setParameters(parameters);
    reconcileExpressions(before, table);
    EXPECT_EQ(table.find(block.extrude)->expressions.at("distance"), "w");
    EXPECT_EQ(constraintOf(table, block.sketch, block.width).expression, "w * 2");

    Document added = before;
    ExtrudeFeature cut;
    cut.profiles = {block.sketch, {{1.0, 1.0}}};
    cut.distance = 12.0;
    const FeatureId typed = added.add(cut);
    added.find(typed)->expressions["distance"] = "w / 2";
    cut.distance = 10.0;
    const FeatureId kept = added.add(cut);
    added.find(kept)->expressions["distance"] = "w / 4";
    added.find(kept)->expressions["taper"] = "w";
    added.find(kept)->expressions["angle"] = "w";
    reconcileExpressions(before, added);
    EXPECT_TRUE(added.find(typed)->expressions.empty()) << "A new value that the expression doesn't give";
    EXPECT_EQ(added.find(kept)->expressions.at("distance"), "w / 4");
    EXPECT_TRUE(added.find(kept)->expressions.contains("taper")) << "An error is left for the stamp to refuse";
    EXPECT_FALSE(added.find(kept)->expressions.contains("angle")) << "A key with no field is dropped";
}

TEST(Parameters, APlainNumberClearsTheExpression)
{
    Rectangle block;
    Document doc = drivenBlock(block);
    ASSERT_FALSE(setFieldExpression(doc, {block.extrude, "distance"}, "12,5"));
    EXPECT_TRUE(doc.find(block.extrude)->expressions.empty());
    EXPECT_NEAR(dataOf<ExtrudeFeature>(doc, block.extrude).distance, 12.5, kTolerance);
    ASSERT_FALSE(setFieldExpression(doc, {block.sketch, {}, block.width}, ""));
    EXPECT_TRUE(constraintOf(doc, block.sketch, block.width).expression.empty());
    EXPECT_NEAR(constraintOf(doc, block.sketch, block.width).value, 80.0, kTolerance);
}

TEST(Parameters, FieldErrorsChangeNothing)
{
    Rectangle block;
    Document doc = drivenBlock(block);
    const std::optional<ExpressionError> unknown = setFieldExpression(doc, {block.extrude, "distance"}, "ww");
    ASSERT_TRUE(unknown);
    EXPECT_EQ(unknown->code, "unknown-name");
    EXPECT_EQ(unknown->hint, "Did you mean 'w'?");
    const std::optional<ExpressionError> kind = setFieldExpression(doc, {block.extrude, "taper"}, "w");
    ASSERT_TRUE(kind);
    EXPECT_EQ(kind->message, "Needs an angle, not a length");
    const std::optional<ExpressionError> key = setFieldExpression(doc, {block.extrude, "depth"}, "w");
    ASSERT_TRUE(key);
    EXPECT_EQ(key->code, "field");
    EXPECT_EQ(doc.find(block.extrude)->expressions.at("distance"), "w");
    EXPECT_FALSE(doc.find(block.extrude)->expressions.contains("taper"));
}

TEST(Parameters, RenameRewritesEveryExpression)
{
    Rectangle block;
    Document doc = drivenBlock(block);
    ASSERT_FALSE(addParameter(doc, "h", "w / 4"));
    ASSERT_FALSE(renameParameter(doc, "w", "width"));
    EXPECT_EQ(namesOf(doc), (std::vector<std::string>{"width", "h"}));
    EXPECT_EQ(doc.parameters()[1].expression, "width / 4");
    EXPECT_EQ(doc.find(block.extrude)->expressions.at("distance"), "width");
    EXPECT_EQ(constraintOf(doc, block.sketch, block.width).expression, "width * 2");

    EXPECT_EQ(renameParameter(doc, "width", "h")->code, "duplicate");
    EXPECT_EQ(renameParameter(doc, "width", "Extrude1")->code, "duplicate");
    EXPECT_EQ(renameParameter(doc, "width", "mm")->code, "reserved");
    EXPECT_EQ(namesOf(doc), (std::vector<std::string>{"width", "h"}));
}

TEST(Parameters, DeleteOfAUsedParameterIsRefused)
{
    Rectangle block;
    Document doc = drivenBlock(block);
    ASSERT_FALSE(addParameter(doc, "h", "w / 4"));
    ASSERT_FALSE(addParameter(doc, "spare", "3 mm"));
    const std::optional<ExpressionError> used = removeParameter(doc, "w");
    ASSERT_TRUE(used);
    EXPECT_EQ(used->code, "used");
    EXPECT_EQ(used->message, "Used by h, Sketch1 d" + std::to_string(block.width) + " and Extrude1 distance");
    ASSERT_FALSE(removeParameter(doc, "spare"));
    EXPECT_EQ(namesOf(doc), (std::vector<std::string>{"w", "h"}));
}

TEST(Parameters, NamesAreSharedWithFeatures)
{
    Rectangle block;
    Document doc = drivenBlock(block);
    EXPECT_EQ(addParameter(doc, "Extrude1", "1 mm")->code, "duplicate");
    EXPECT_EQ(addParameter(doc, "w", "1 mm")->code, "duplicate");
    EXPECT_EQ(addParameter(doc, "XY", "1 mm")->code, "reserved");
    EXPECT_EQ(checkFeatureName(doc, "w")->code, "duplicate");
    EXPECT_FALSE(checkFeatureName(doc, "Base"));

    ASSERT_FALSE(addParameter(doc, "Sketch2", "1 mm"));
    EXPECT_EQ(doc.find(doc.add(SketchFeature{}))->name, "Sketch3");
}

TEST(Parameters, StampsEveryListedField)
{
    Document doc;
    ASSERT_FALSE(addParameter(doc, "L", "7 mm"));
    ASSERT_FALSE(addParameter(doc, "A", "40 deg"));
    ASSERT_FALSE(addParameter(doc, "N", "4"));

    SketchFeature sketchData;
    sketch::Sketch& s = sketchData.sketch;
    const sketch::Id p0 = s.addPoint({0, 0});
    const sketch::Id p1 = s.addPoint({10, 0});
    const sketch::Id p2 = s.addPoint({10, 10});
    const sketch::Id line0 = s.addLine(p0, p1);
    const sketch::Id line1 = s.addLine(p1, p2);
    const sketch::Id circle = s.addCircle(s.addPoint({30, 0}), 5.0);
    const sketch::Id distance = s.addConstraint(sketch::ConstraintKind::Distance, {p0, p1}, 10.0);
    const sketch::Id across = s.addConstraint(sketch::ConstraintKind::HorizontalDistance, {p0, p2}, 10.0);
    const sketch::Id up = s.addConstraint(sketch::ConstraintKind::VerticalDistance, {p1, p2}, 10.0);
    const sketch::Id radius = s.addConstraint(sketch::ConstraintKind::Radius, {circle}, 5.0);
    const sketch::Id diameter = s.addConstraint(sketch::ConstraintKind::Diameter, {circle}, 10.0);
    const sketch::Id corner = s.addConstraint(sketch::ConstraintKind::Angle, {line0, line1}, 90.0);
    const sketch::Id square = s.addConstraint(sketch::ConstraintKind::Perpendicular, {line0, line1});
    const FeatureId sketchId = doc.add(std::move(sketchData));

    ExtrudeFeature extrudeData;
    extrudeData.distance = -10.0;
    const FeatureId extrude = doc.add(extrudeData);
    const FeatureId revolve = doc.add(RevolveFeature{});
    FilletFeature filletData;
    filletData.sets.push_back(FilletSet{});
    const FeatureId fillet = doc.add(filletData);
    const FeatureId chamfer = doc.add(ChamferFeature{});
    const FeatureId shell = doc.add(ShellFeature{});
    const FeatureId offsetFace = doc.add(OffsetFaceFeature{});
    const FeatureId hole = doc.add(HoleFeature{});
    const FeatureId offsetPlane = doc.add(PlaneFeature{});
    PlaneFeature angleData;
    angleData.kind = PlaneKind::Angle;
    const FeatureId anglePlane = doc.add(angleData);
    const FeatureId pattern = doc.add(PatternFeature{});

    struct Case {
        FieldRef field;
        const char* text;
        double expected;
        std::function<double(const Document&)> read;
    };
    const auto dimension = [&](sketch::Id id) {
        return [=](const Document& d) { return constraintOf(d, sketchId, id).value; };
    };
    const Case cases[] = {
        {{sketchId, {}, distance}, "L", 7.0, dimension(distance)},
        {{sketchId, {}, across}, "L * 2", 14.0, dimension(across)},
        {{sketchId, {}, up}, "L + 1", 8.0, dimension(up)},
        {{sketchId, {}, radius}, "L / 2", 3.5, dimension(radius)},
        {{sketchId, {}, diameter}, "L", 7.0, dimension(diameter)},
        {{sketchId, {}, corner}, "A", 40.0, dimension(corner)},
        {{extrude, "distance"},
         "L",
         -7.0,
         [&](const Document& d) { return dataOf<ExtrudeFeature>(d, extrude).distance; }},
        {{extrude, "second_distance"},
         "L * 3",
         21.0,
         [&](const Document& d) { return dataOf<ExtrudeFeature>(d, extrude).secondDistance; }},
        {{extrude, "taper"},
         "A / 4",
         10.0,
         [&](const Document& d) { return dataOf<ExtrudeFeature>(d, extrude).taperDegrees; }},
        {{revolve, "angle"},
         "A * 2",
         80.0,
         [&](const Document& d) { return dataOf<RevolveFeature>(d, revolve).angleDegrees; }},
        {{fillet, "sets.0.radius"},
         "L",
         7.0,
         [&](const Document& d) { return dataOf<FilletFeature>(d, fillet).sets[0].radius; }},
        {{fillet, "sets.1.radius"},
         "L / 7",
         1.0,
         [&](const Document& d) { return dataOf<FilletFeature>(d, fillet).sets[1].radius; }},
        {{chamfer, "distance"},
         "L",
         7.0,
         [&](const Document& d) { return dataOf<ChamferFeature>(d, chamfer).distance; }},
        {{shell, "thickness"}, "L", 7.0, [&](const Document& d) { return dataOf<ShellFeature>(d, shell).thickness; }},
        {{offsetFace, "distance"},
         "-L",
         -7.0,
         [&](const Document& d) { return dataOf<OffsetFaceFeature>(d, offsetFace).distance; }},
        {{hole, "diameter"}, "L", 7.0, [&](const Document& d) { return dataOf<HoleFeature>(d, hole).diameter; }},
        {{hole, "depth"}, "L * 2", 14.0, [&](const Document& d) { return dataOf<HoleFeature>(d, hole).depth; }},
        {{hole, "head_diameter"},
         "L * 3",
         21.0,
         [&](const Document& d) { return dataOf<HoleFeature>(d, hole).headDiameter; }},
        {{hole, "head_depth"}, "L / 2", 3.5, [&](const Document& d) { return dataOf<HoleFeature>(d, hole).headDepth; }},
        {{hole, "countersink_angle"},
         "A * 2",
         80.0,
         [&](const Document& d) { return dataOf<HoleFeature>(d, hole).countersinkAngle; }},
        {{hole, "tip_angle"}, "A * 3", 120.0, [&](const Document& d) { return dataOf<HoleFeature>(d, hole).tipAngle; }},
        {{offsetPlane, "offset"},
         "L * 10",
         70.0,
         [&](const Document& d) { return dataOf<PlaneFeature>(d, offsetPlane).offset; }},
        {{anglePlane, "angle"},
         "-A",
         -40.0,
         [&](const Document& d) { return dataOf<PlaneFeature>(d, anglePlane).angleDegrees; }},
        {{pattern, "count"},
         "N + 1",
         5.0,
         [&](const Document& d) { return static_cast<double>(dataOf<PatternFeature>(d, pattern).count); }},
        {{pattern, "spacing"},
         "L * 4",
         28.0,
         [&](const Document& d) { return dataOf<PatternFeature>(d, pattern).spacing; }},
        {{pattern, "angle"},
         "A * 9",
         360.0,
         [&](const Document& d) { return dataOf<PatternFeature>(d, pattern).angleDegrees; }},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(std::string(c.field.key) + " " + c.text);
        const std::optional<ExpressionError> error = setFieldExpression(doc, c.field, c.text);
        ASSERT_FALSE(error) << error->message;
        EXPECT_NEAR(c.read(doc), c.expected, kTolerance);
    }

    ASSERT_FALSE(setParameterExpression(doc, "L", "5 mm"));
    EXPECT_NEAR(constraintOf(doc, sketchId, across).value, 10.0, kTolerance);
    EXPECT_NEAR(dataOf<ExtrudeFeature>(doc, extrude).distance, -5.0, kTolerance);
    EXPECT_NEAR(dataOf<HoleFeature>(doc, hole).headDiameter, 15.0, kTolerance);
    EXPECT_NEAR(dataOf<PatternFeature>(doc, pattern).spacing, 20.0, kTolerance);

    EXPECT_EQ(setFieldExpression(doc, {offsetPlane, "angle"}, "A")->code, "field");
    EXPECT_EQ(setFieldExpression(doc, {fillet, "sets.2.radius"}, "L")->code, "field");
    EXPECT_EQ(setFieldExpression(doc, {sketchId, {}, square}, "L")->code, "field");
}

TEST(Parameters, InvalidStampsAreRefusedWithTheFieldAndValue)
{
    Rectangle block;
    Document doc = drivenBlock(block);
    ASSERT_FALSE(addParameter(doc, "n", "3"));
    const PatternFeature patternData{{block.extrude, 0}};
    const FeatureId pattern = doc.add(patternData);

    const std::optional<ExpressionError> zero = setParameterExpression(doc, "w", "0 mm");
    ASSERT_TRUE(zero);
    EXPECT_EQ(zero->code, "stamp");
    EXPECT_EQ(zero->message, "Sketch1 d" + std::to_string(block.width) + " can't be 0 mm");
    EXPECT_EQ(doc.parameters().front().expression, "40 mm");

    const std::optional<ExpressionError> negative = setFieldExpression(doc, {block.extrude, "distance"}, "-w");
    ASSERT_TRUE(negative);
    EXPECT_EQ(negative->message, "Extrude1 distance can't be -40 mm");

    ASSERT_FALSE(setFieldExpression(doc, {pattern, "count"}, "n"));
    const std::optional<ExpressionError> half = setParameterExpression(doc, "n", "2.5");
    ASSERT_TRUE(half);
    EXPECT_EQ(half->message, "Pattern1 count takes a whole number, not 2.5");
    const std::optional<ExpressionError> many = setParameterExpression(doc, "n", "101");
    ASSERT_TRUE(many);
    EXPECT_EQ(many->message, "Pattern1 count can't be 101");
    EXPECT_EQ(many->hint, "Use a value from 2 to 100");
    EXPECT_EQ(dataOf<PatternFeature>(doc, pattern).count, 3);

    const std::optional<ExpressionError> steep = setFieldExpression(doc, {block.extrude, "taper"}, "w * 3 deg / 1 mm");
    ASSERT_TRUE(steep);
    EXPECT_EQ(steep->message, "Extrude1 taper can't be 120 deg");
}

TEST(Parameters, OneEditIsOneUndoStep)
{
    Rectangle block;
    Document doc = drivenBlock(block);
    UndoStack undo;
    undo.push("Edit w", doc);
    ASSERT_FALSE(setParameterExpression(doc, "w", "25 mm"));
    EXPECT_NEAR(dataOf<ExtrudeFeature>(doc, block.extrude).distance, 25.0, kTolerance);
    undo.undo(doc);
    EXPECT_EQ(doc.parameters().front().expression, "40 mm");
    EXPECT_NEAR(dataOf<ExtrudeFeature>(doc, block.extrude).distance, 40.0, kTolerance);
    EXPECT_NEAR(constraintOf(doc, block.sketch, block.width).value, 80.0, kTolerance);
    undo.redo(doc);
    EXPECT_NEAR(constraintOf(doc, block.sketch, block.width).value, 50.0, kTolerance);
}

TEST(ParameterFiles, RoundTripWritesOnlyWhatIsSet)
{
    Rectangle block;
    Document doc = drivenBlock(block);
    ASSERT_FALSE(addParameter(doc, "h", "w / 2"));
    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    const nlohmann::json saved = nlohmann::json::parse(text);
    EXPECT_EQ(saved["version"], 8);
    EXPECT_EQ(saved["parameters"], nlohmann::json::parse(R"([{"name": "w", "expression": "40 mm"},
                                                             {"name": "h", "expression": "w / 2"}])"));
    EXPECT_EQ(saved["features"][1]["data"]["expressions"], nlohmann::json::parse(R"({"distance": "w"})"));
    EXPECT_EQ(saved["features"][1]["data"]["distance"], 40.0);
    std::size_t driven = 0;
    for (const nlohmann::json& constraint : saved["features"][0]["data"]["sketch"]["constraints"]) {
        if (constraint.contains("expression")) {
            EXPECT_EQ(constraint["expression"], "w * 2");
            EXPECT_EQ(constraint["value"], 80.0);
            ++driven;
        }
    }
    EXPECT_EQ(driven, 1U);
    const Document loaded = fromJson(text, shapes);
    EXPECT_EQ(toJson(loaded, shapes), text);

    Document plain;
    addBlock(plain, 10.0, 10.0, 10.0);
    const std::string bare = toJson(plain, shapes);
    EXPECT_EQ(bare.find("parameters"), std::string::npos);
    EXPECT_EQ(bare.find("expression"), std::string::npos);
}

TEST(ParameterFiles, TheExpressionWinsOverAStaleValue)
{
    Rectangle block;
    ShapeStore shapes;
    nlohmann::json design = nlohmann::json::parse(toJson(drivenBlock(block), shapes));
    design["features"][1]["data"]["distance"] = 3.0;
    design["features"][1]["data"]["expressions"]["distance"] = "w+5";
    const Document loaded = fromJson(design.dump(), shapes);
    EXPECT_NEAR(dataOf<ExtrudeFeature>(loaded, block.extrude).distance, 45.0, kTolerance);
    EXPECT_EQ(loaded.find(block.extrude)->expressions.at("distance"), "w + 5 mm");
}

TEST(ParameterFiles, RejectsDamagedTables)
{
    Rectangle block;
    ShapeStore shapes;
    const nlohmann::json good = nlohmann::json::parse(toJson(drivenBlock(block), shapes));
    ASSERT_NO_THROW(fromJson(good.dump(), shapes));
    const auto rejects = [&](const char* what, const std::function<void(nlohmann::json&)>& damage) {
        nlohmann::json design = good;
        damage(design);
        try {
            fromJson(design.dump(), shapes);
            ADD_FAILURE() << what << " loaded";
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find("damaged"), std::string::npos) << what << ": " << error.what();
        }
    };
    const auto parameter = [](const char* name, const char* expression) {
        return nlohmann::json{{"name", name}, {"expression", expression}};
    };
    rejects("bad name", [&](nlohmann::json& d) { d["parameters"][0]["name"] = "1w"; });
    rejects("unit as name", [&](nlohmann::json& d) { d["parameters"].push_back(parameter("mm", "1 mm")); });
    rejects("duplicate", [&](nlohmann::json& d) { d["parameters"].push_back(parameter("w", "1 mm")); });
    rejects("feature name", [&](nlohmann::json& d) { d["parameters"].push_back(parameter("Extrude1", "1 mm")); });
    rejects("names one further down", [&](nlohmann::json& d) {
        d["parameters"] = nlohmann::json::array({parameter("h", "w / 2"), parameter("w", "40 mm")});
    });
    rejects("expression does not parse", [&](nlohmann::json& d) { d["parameters"][0]["expression"] = "40 +"; });
    rejects("empty expression", [&](nlohmann::json& d) { d["parameters"][0]["expression"] = ""; });
    rejects("expression not text", [&](nlohmann::json& d) { d["parameters"][0]["expression"] = 40; });
    rejects("table not a list", [&](nlohmann::json& d) { d["parameters"] = parameter("w", "40 mm"); });
    rejects("unknown field key", [&](nlohmann::json& d) { d["features"][1]["data"]["expressions"]["depth"] = "w"; });
    rejects("field expression does not parse",
            [&](nlohmann::json& d) { d["features"][1]["data"]["expressions"]["distance"] = "w *"; });
    rejects("field of another kind",
            [&](nlohmann::json& d) { d["features"][1]["data"]["expressions"]["taper"] = "w"; });
    rejects("unknown name in a field",
            [&](nlohmann::json& d) { d["features"][1]["data"]["expressions"]["distance"] = "q"; });
    rejects("expression on a non dimension",
            [&](nlohmann::json& d) { d["features"][0]["data"]["sketch"]["constraints"][0]["expression"] = "w"; });
    rejects("invalid stamp", [&](nlohmann::json& d) { d["parameters"][0]["expression"] = "-40 mm"; });
}
