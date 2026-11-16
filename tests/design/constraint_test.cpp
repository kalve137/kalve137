// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lang/edit.h"
#include "lang/format.h"
#include "lang/parse.h"
#include "rebuild/rebuild.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace kalve137;

const lang::Diagnostic* first(const model::Snapshot& snapshot, lang::Diagnostic::Severity severity)
{
    for (const lang::Diagnostic& d : snapshot.diagnostics) {
        if (d.severity == severity) {
            return &d;
        }
    }
    return nullptr;
}

const lang::Diagnostic* firstError(const model::Snapshot& snapshot)
{
    return first(snapshot, lang::Diagnostic::Severity::Error);
}

std::string replaced(std::string text, const std::string& from, const std::string& to)
{
    const std::size_t at = text.find(from);
    EXPECT_NE(at, std::string::npos) << from;
    text.replace(at, from.size(), to);
    return text;
}

constexpr const char* kTab = R"(param width = 40 mm
param height = 20 mm

sketch tab on XY {
  point a near (0 mm, 0 mm)
  point b near (38 mm, 1 mm)
  point c near (41 mm, 19 mm)
  point d near (1 mm, 21 mm)
  line base from a to b
  line right from b to c
  line top from c to d
  line left from d to a
  at(a) = (0 mm, 0 mm)
  horizontal(base)
  vertical(right)
  horizontal(top)
  vertical(left)
  length(base) = width
  length(left) = height
}

extrude body = tab up 5 mm
)";

TEST(ConstraintTest, AFullyConstrainedLoopBuildsWithSidesNamedAfterItsLines)
{
    const model::Snapshot snapshot = rebuild::build(kTab, {false, {}});
    ASSERT_EQ(firstError(snapshot), nullptr) << firstError(snapshot)->message;
    EXPECT_EQ(first(snapshot, lang::Diagnostic::Severity::Warning), nullptr);
    EXPECT_NEAR(snapshot.boxMax.x - snapshot.boxMin.x, 40.0, 1e-6);
    EXPECT_NEAR(snapshot.boxMax.y - snapshot.boxMin.y, 20.0, 1e-6);
    for (const char* name :
         {"body.tab.base", "body.tab.right", "body.tab.top", "body.tab.left", "body.tab.start", "body.tab.end"}) {
        EXPECT_GE(snapshot.faceIndex(name), 0) << name;
    }
    const model::Face& base = snapshot.faces[snapshot.faceIndex("body.tab.base")];
    EXPECT_NEAR(base.normal.y, -1.0, 1e-9);
}

TEST(ConstraintTest, AParamChangeMovesTheSolvedShapeAndKeepsTheNames)
{
    const model::Snapshot snapshot = rebuild::build(kTab, {false, {{"width", 60.0}}});
    ASSERT_EQ(firstError(snapshot), nullptr) << firstError(snapshot)->message;
    EXPECT_NEAR(snapshot.boxMax.x - snapshot.boxMin.x, 60.0, 1e-6);
    EXPECT_GE(snapshot.faceIndex("body.tab.right"), 0);
}

TEST(ConstraintTest, TheSameSourceAlwaysSolvesToTheSameShape)
{
    const model::Snapshot once = rebuild::build(kTab, {false, {}});
    const model::Snapshot again = rebuild::build(kTab, {false, {}});
    ASSERT_EQ(once.faces.size(), again.faces.size());
    for (std::size_t i = 0; i < once.faces.size(); ++i) {
        EXPECT_EQ(once.faces[i].name, again.faces[i].name);
        EXPECT_EQ(once.faces[i].centroid.x, again.faces[i].centroid.x);
        EXPECT_EQ(once.faces[i].centroid.y, again.faces[i].centroid.y);
    }
}

TEST(ConstraintTest, FreedomLeftIsAWarningThatNamesWhatCanMove)
{
    const model::Snapshot snapshot = rebuild::build(replaced(kTab, "  length(left) = height\n", ""), {false, {}});
    ASSERT_EQ(firstError(snapshot), nullptr) << firstError(snapshot)->message;
    EXPECT_TRUE(snapshot.built);
    const lang::Diagnostic* warning = first(snapshot, lang::Diagnostic::Severity::Warning);
    ASSERT_NE(warning, nullptr);
    EXPECT_EQ(warning->code, "free");
    EXPECT_EQ(warning->statement, "tab");
    EXPECT_NE(warning->message.find("1 degree of freedom"), std::string::npos) << warning->message;
    EXPECT_NE(warning->message.find("c and d"), std::string::npos) << warning->message;
}

TEST(ConstraintTest, ARepeatedConstraintIsAnErrorThatQuotesIt)
{
    const model::Snapshot snapshot = rebuild::build(
        replaced(kTab, "  at(a) = (0 mm, 0 mm)\n", "  at(a) = (0 mm, 0 mm)\n  parallel(base, top)\n"), {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "over") << error->message;
    EXPECT_NE(error->message.find("'parallel(base, top)'"), std::string::npos) << error->message;
    EXPECT_NE(error->message.find("'horizontal(top)'"), std::string::npos) << error->message;
    EXPECT_EQ(error->message.find("length("), std::string::npos) << error->message;
    EXPECT_NE(error->hint.find("like 'horizontal(top)'"), std::string::npos) << error->hint;
}

TEST(ConstraintTest, ConstraintsThatDisagreeAreAnErrorThatQuotesThem)
{
    const model::Snapshot snapshot = rebuild::build(
        replaced(kTab, "  length(left) = height\n", "  length(left) = height\n  length(right) = 30 mm\n"), {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "conflict") << error->message;
    EXPECT_NE(error->message.find("'length(right) = 30 mm'"), std::string::npos) << error->message;
    EXPECT_NE(error->message.find("'length(left) = height'"), std::string::npos) << error->message;
    EXPECT_EQ(error->message.find("length(base)"), std::string::npos) << error->message;
}

TEST(ConstraintTest, ASolveThatTurnsAgainstTheWrittenShapeFailsLoudly)
{
    std::string source = replaced(kTab, "point c near (41 mm, 19 mm)", "point c near (1 mm, 19 mm)");
    source = replaced(source, "point d near (1 mm, 21 mm)", "point d near (41 mm, 21 mm)");
    const model::Snapshot snapshot = rebuild::build(source, {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "flip");
    EXPECT_FALSE(snapshot.built);
}

constexpr const char* kSlot = R"(sketch slot on XY {
  point a near (0 mm, 0 mm)
  point b near (40 mm, 0 mm)
  point c near (40 mm, 20 mm)
  point d near (0 mm, 20 mm)
  point e near (40 mm, 10 mm)
  point f near (0 mm, 10 mm)
  line base from a to b
  arc right from b to c center e
  line top from c to d
  arc left from d to a center f
  at(a) = (0 mm, 0 mm)
  horizontal(base)
  tangent(base, right)
  tangent(right, top)
  tangent(top, left)
  tangent(left, base)
  equal(left, right)
  length(base) = 40 mm
  radius(right) = 10 mm
}

extrude body = slot up 5 mm
)";

TEST(ConstraintTest, ArcsAndTangentsMakeASlot)
{
    const model::Snapshot snapshot = rebuild::build(kSlot, {false, {}});
    ASSERT_EQ(firstError(snapshot), nullptr) << firstError(snapshot)->message;
    EXPECT_EQ(first(snapshot, lang::Diagnostic::Severity::Warning), nullptr);
    EXPECT_NEAR(snapshot.boxMax.x - snapshot.boxMin.x, 60.0, 1e-6);
    EXPECT_NEAR(snapshot.boxMax.y - snapshot.boxMin.y, 20.0, 1e-6);
    const int right = snapshot.faceIndex("body.slot.right");
    ASSERT_GE(right, 0);
    EXPECT_EQ(snapshot.faces[right].kind, "cylinder");
    EXPECT_NEAR(snapshot.faces[right].radius, 10.0, 1e-6);
}

TEST(ConstraintTest, AnOpenLoopNamesWhereItIsOpen)
{
    const model::Snapshot snapshot = rebuild::build(R"(sketch v on XY {
  point a near (0 mm, 0 mm)
  point b near (40 mm, 0 mm)
  point c near (20 mm, 20 mm)
  line one from a to b
  line two from b to c
  at(a) = (0 mm, 0 mm)
}

extrude body = v up 5 mm
)",
                                                    {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "open");
    EXPECT_NE(error->message.find("point a"), std::string::npos) << error->message;
}

TEST(ConstraintTest, AConstraintOnTheWrongKindsSaysWhichFormsFit)
{
    const model::Snapshot snapshot = rebuild::build(
        replaced(kTab, "  at(a) = (0 mm, 0 mm)\n", "  at(a) = (0 mm, 0 mm)\n  parallel(base, a)\n"), {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "constraint");
    EXPECT_NE(error->message.find("a is a point"), std::string::npos) << error->message;
    EXPECT_NE(error->hint.find("parallel(line, line)"), std::string::npos) << error->hint;
}

TEST(ConstraintTest, ALineIsASideOfTheLoopNotAnItem)
{
    const model::Snapshot snapshot = rebuild::build(
        std::string(kTab) + "\nsketch mark on body.base.end {\n  circle spot center (20 mm, 10 mm) diameter 4 mm\n}\n",
        {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "selector");
    EXPECT_NE(error->hint.find("body.tab.base"), std::string::npos) << error->hint;
}

TEST(ConstraintTest, AnAddedConstraintTakesUpTheFreedomItNames)
{
    const std::string loose = replaced(kTab, "  length(left) = height\n", "");
    const std::string held = lang::addConstraint(loose, "tab", "length(left) = 20 mm");
    EXPECT_NE(held.find("  length(base) = width\n  length(left) = 20 mm\n}"), std::string::npos) << held;
    const model::Snapshot snapshot = rebuild::build(held, {false, {}});
    EXPECT_EQ(firstError(snapshot), nullptr);
    EXPECT_EQ(first(snapshot, lang::Diagnostic::Severity::Warning), nullptr);
    ASSERT_EQ(snapshot.sketches.size(), 1U);
    EXPECT_EQ(snapshot.sketches.front().freedom, 0);
    EXPECT_THROW(lang::addConstraint(loose, "nope", "length(left) = 20 mm"), std::runtime_error);
}

TEST(ConstraintTest, ConstraintsFormatBackToTheirText)
{
    const lang::ParseResult parsed = lang::parse(kSlot);
    ASSERT_TRUE(parsed.diagnostics.empty());
    EXPECT_EQ(lang::format(parsed.document), kSlot);
}

constexpr const char* kNose = R"(param reach = 50 mm
param width = 20 mm

sketch outline on XY {
  point a
  point b near (40 mm, 0 mm)
  point c near (40 mm, 20 mm)
  point d near (0 mm, 20 mm)
  point e near (40 mm, 10 mm)
  line base from a to b
  arc nose from b to c center e
  line top from c to d
  line back from d to a
  at(a) = (0 mm, 0 mm)
  horizontal(base)
  horizontal(top)
  vertical(back)
  tangent(base, nose)
  tangent(nose, top)
  length(base) = reach - width / 2
  length(back) = width
}

extrude body = outline up 4 mm
)";

TEST(ConstraintTest, TheNoseBuildsFromAPinAndGuesses)
{
    const model::Snapshot snapshot = rebuild::build(kNose, {false, {}});
    ASSERT_EQ(firstError(snapshot), nullptr) << firstError(snapshot)->message;
    EXPECT_EQ(first(snapshot, lang::Diagnostic::Severity::Warning), nullptr);
    EXPECT_NEAR(snapshot.boxMax.x - snapshot.boxMin.x, 50.0, 1e-6);
}

TEST(ConstraintTest, AReversedArcPrintsTheLineToWriteInstead)
{
    const std::string source = replaced(kNose, "arc nose from b to c center e", "arc nose from c to b center e");
    const model::Snapshot snapshot = rebuild::build(source, {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_TRUE(error->code == "cusp" || error->code == "flip") << error->code << ": " << error->message;
    EXPECT_NE(error->hint.find("Write: arc nose from b to c center e"), std::string::npos) << error->hint;
    const model::Snapshot fixed = rebuild::build(kNose, {false, {}});
    EXPECT_EQ(firstError(fixed), nullptr);
}

TEST(ConstraintTest, AFlipPrintsGuessesThatBuildOncePasted)
{
    std::string source = replaced(kTab, "point c near (41 mm, 19 mm)", "point c near (1 mm, 19 mm)");
    source = replaced(source, "point d near (1 mm, 21 mm)", "point d near (41 mm, 21 mm)");
    const model::Snapshot snapshot = rebuild::build(source, {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    ASSERT_EQ(error->code, "flip");
    std::size_t pasted = 0;
    std::size_t start = error->hint.find("\n  point ");
    while (start != std::string::npos) {
        const std::size_t end = error->hint.find('\n', start + 1);
        const std::string line = error->hint.substr(start + 3, end - start - 3);
        const std::string name = line.substr(0, line.find(" near"));
        const std::size_t old = source.find("  " + name + " near");
        ASSERT_NE(old, std::string::npos) << name;
        source.replace(old + 2, source.find('\n', old) - old - 2, line);
        ++pasted;
        start = error->hint.find("\n  point ", end);
    }
    EXPECT_GE(pasted, 2U) << error->hint;
    const model::Snapshot fixed = rebuild::build(source, {false, {}});
    EXPECT_EQ(firstError(fixed), nullptr) << source << "\n" << firstError(fixed)->message;
}

TEST(ConstraintTest, PinsTakeParamsAndGuessesFormatBack)
{
    const std::string source = R"(param width = 40 mm
param height = 20 mm

sketch tab on XY {
  point a
  point b near (38 mm, 1 mm)
  point c near (41 mm, 19 mm)
  point d near (1 mm, 21 mm)
  line base from a to b
  line right from b to c
  line top from c to d
  line left from d to a
  at(a) = (-width / 2, 0 mm)
  x(c) = width / 2
  horizontal(base)
  vertical(right)
  horizontal(top)
  vertical(left)
  length(left) = height
}

extrude body = tab up 5 mm
)";
    const lang::ParseResult parsed = lang::parse(source);
    ASSERT_TRUE(parsed.diagnostics.empty());
    EXPECT_EQ(lang::format(parsed.document), source);
    const model::Snapshot snapshot = rebuild::build(source, {false, {}});
    ASSERT_EQ(firstError(snapshot), nullptr) << firstError(snapshot)->message;
    EXPECT_NEAR(snapshot.boxMin.x, -20.0, 1e-6);
    EXPECT_NEAR(snapshot.boxMax.x, 20.0, 1e-6);
    const model::Snapshot wider = rebuild::build(source, {false, {{"width", 60.0}}});
    ASSERT_EQ(firstError(wider), nullptr) << firstError(wider)->message;
    EXPECT_NEAR(wider.boxMin.x, -30.0, 1e-6);
    EXPECT_NEAR(wider.boxMax.x, 30.0, 1e-6);
}

TEST(ConstraintTest, AGuessWrittenAsAFactIsRefusedWithTheLineToWrite)
{
    const model::Snapshot at =
        rebuild::build(replaced(kTab, "point b near (38 mm, 1 mm)", "point b at (38 mm, 1 mm)"), {false, {}});
    ASSERT_NE(firstError(at), nullptr);
    EXPECT_EQ(firstError(at)->code, "guess");
    EXPECT_NE(firstError(at)->hint.find("point b near (38 mm, 1 mm)"), std::string::npos) << firstError(at)->hint;

    const model::Snapshot param =
        rebuild::build(replaced(kTab, "point b near (38 mm, 1 mm)", "point b near (width, 1 mm)"), {false, {}});
    ASSERT_NE(firstError(param), nullptr);
    EXPECT_EQ(firstError(param)->code, "guess");
    EXPECT_NE(firstError(param)->hint.find("x(b) = width"), std::string::npos) << firstError(param)->hint;

    const model::Snapshot none = rebuild::build(replaced(kTab, "point b near (38 mm, 1 mm)", "point b"), {false, {}});
    ASSERT_NE(firstError(none), nullptr);
    EXPECT_EQ(firstError(none)->code, "guess");
}

TEST(ConstraintTest, FixIsGoneAndItsHintPinsTheGuess)
{
    const model::Snapshot snapshot =
        rebuild::build(replaced(kTab, "  at(a) = (0 mm, 0 mm)\n", "  fix(a)\n"), {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "constraint");
    EXPECT_NE(error->hint.find("Write at(a) = (0 mm, 0 mm)"), std::string::npos) << error->hint;
}

TEST(ConstraintTest, BothCoordinatesArePinnedWithAt)
{
    const model::Snapshot snapshot =
        rebuild::build(replaced(kTab, "  at(a) = (0 mm, 0 mm)\n", "  x(a) = 0 mm\n  y(a) = 0 mm\n"), {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "constraint");
    EXPECT_NE(error->hint.find("Write at(a) = (0 mm, 0 mm)"), std::string::npos) << error->hint;
}

std::string settle(const std::string& source)
{
    const model::Snapshot snapshot = rebuild::build(source, {false, {}});
    lang::SketchPositions solved;
    for (const model::SolvedSketch& sketch : snapshot.sketches) {
        for (const model::SolvedPoint& p : sketch.points) {
            solved[sketch.name][p.name] = {p.u, p.v};
        }
    }
    return lang::settleGuesses(source, solved);
}

TEST(ConstraintTest, SettlingWritesTheSolutionIntoTheGuessesOnce)
{
    const std::string settled = settle(kTab);
    EXPECT_NE(settled.find("  point b near (40 mm, 0 mm)\n"), std::string::npos) << settled;
    EXPECT_NE(settled.find("  point c near (40 mm, 20 mm)\n"), std::string::npos) << settled;
    EXPECT_NE(settled.find("  point d near (0 mm, 20 mm)\n"), std::string::npos) << settled;
    EXPECT_EQ(settle(settled), settled);

    const model::Snapshot before = rebuild::build(kTab, {false, {}});
    const model::Snapshot after = rebuild::build(settled, {false, {}});
    ASSERT_EQ(firstError(after), nullptr) << firstError(after)->message;
    ASSERT_EQ(before.faces.size(), after.faces.size());
    for (std::size_t i = 0; i < before.faces.size(); ++i) {
        EXPECT_EQ(before.faces[i].name, after.faces[i].name);
        EXPECT_NEAR(before.faces[i].centroid.x, after.faces[i].centroid.x, 1e-9);
        EXPECT_NEAR(before.faces[i].centroid.y, after.faces[i].centroid.y, 1e-9);
        EXPECT_NEAR(before.faces[i].area, after.faces[i].area, 1e-9);
    }
}

TEST(ConstraintTest, FormattingAndBuildingNeverTouchTheGuesses)
{
    const lang::ParseResult parsed = lang::parse(kTab);
    ASSERT_TRUE(parsed.diagnostics.empty());
    const std::string formatted = lang::format(parsed.document);
    EXPECT_EQ(formatted, kTab);
    EXPECT_NE(formatted.find("  point b near (38 mm, 1 mm)\n"), std::string::npos);
}

TEST(ConstraintTest, AShapeThatDependsOnItsGuessesIsABranchError)
{
    const std::string plate = R"(sketch plate on XY {
  point a
  point b
  point c
  point d
  point e near (20 mm, 1 mm)
  line base from a to b
  line right from b to c
  line top from c to d
  line left from d to a
  circle bore center e diameter 4 mm
  at(a) = (0 mm, 0 mm)
  at(b) = (40 mm, 0 mm)
  at(c) = (40 mm, 20 mm)
  at(d) = (0 mm, 20 mm)
  distance(a, e) = 21 mm
  distance(b, e) = 21 mm
}

extrude body = plate up 2 mm
)";
    const model::Snapshot low = rebuild::build(plate, {false, {}});
    const lang::Diagnostic* error = firstError(low);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "branch") << error->message;
    EXPECT_NE(error->message.find("e at (20 mm, -6.403124 mm)"), std::string::npos) << error->message;

    std::string deep = replaced(replaced(plate, "21 mm", "25 mm"), "21 mm", "25 mm");
    deep = replaced(replaced(deep, "20 mm)", "40 mm)"), "20 mm)", "40 mm)");
    deep = replaced(deep, "near (20 mm, 1 mm)", "near (20 mm, 15 mm)");
    const model::Snapshot settled = rebuild::build(deep, {false, {}});
    EXPECT_EQ(firstError(settled), nullptr) << firstError(settled)->message;
}

TEST(ConstraintTest, AShapeThatWouldFlipIsNotABranch)
{
    const std::string triangle = R"(sketch tri on XY {
  point a
  point b
  point c near (20 mm, 1 mm)
  line base from a to b
  line right from b to c
  line left from c to a
  at(a) = (0 mm, 0 mm)
  at(b) = (40 mm, 0 mm)
  distance(a, c) = 21 mm
  distance(b, c) = 21 mm
}

extrude body = tri up 2 mm
)";
    const model::Snapshot built = rebuild::build(triangle, {false, {}});
    EXPECT_EQ(firstError(built), nullptr) << firstError(built)->message;
}

TEST(ConstraintTest, TheSolveDoesNotDependOnTheHeap)
{
    const std::string source = R"(sketch block on XZ {
  point a
  point b near (28 mm, 0 mm)
  point c near (28 mm, 8 mm)
  point d near (17 mm, 8 mm)
  point e near (17 mm, 22 mm)
  point f near (-17 mm, 22 mm)
  point g near (-17 mm, 8 mm)
  point h near (-28 mm, 8 mm)
  point o near (0 mm, 22 mm)
  line bottom from a to b
  line right from b to c
  line shelf_r from c to d
  line side_r from d to e
  arc top from e to f center o
  line side_l from f to g
  line shelf_l from g to h
  line left from h to a
  at(a) = (-28 mm, 0 mm)
  horizontal(bottom)
  vertical(right)
  horizontal(shelf_r)
  vertical(side_r)
  vertical(side_l)
  horizontal(shelf_l)
  vertical(left)
  tangent(side_r, top)
  tangent(side_l, top)
  length(bottom) = 56 mm
  length(right) = 8 mm
  horizontal(c, h)
  equal(shelf_r, shelf_l)
  distance(d, g) = 34 mm
  distance(o, bottom) = 22 mm
}

extrude body = block up 12 mm
)";
    std::vector<std::unique_ptr<double[]>> clutter;
    std::string first;
    for (int round = 0; round < 24; ++round) {
        clutter.push_back(std::make_unique<double[]>(static_cast<std::size_t>(1 + (round * 7) % 5)));
        if (round % 3 == 0) {
            clutter.erase(clutter.begin());
        }
        const model::Snapshot snapshot = rebuild::build(source, {false, {}});
        const lang::Diagnostic* error = firstError(snapshot);
        const std::string outcome = error ? error->code + ": " + error->message : "built";
        if (round == 0) {
            first = outcome;
        }
        EXPECT_EQ(outcome, first) << "round " << round;
    }
    EXPECT_EQ(first, "built");
}

} // namespace
