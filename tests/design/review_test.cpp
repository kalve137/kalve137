// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lang/edit.h"
#include "lang/format.h"
#include "lang/parse.h"
#include "model/json.h"
#include "model/select.h"
#include "rebuild/rebuild.h"
#include "rebuild/suggest.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

using namespace kalve137;

std::string example(const std::string& name)
{
    std::ifstream in(std::filesystem::path(KALVE137_SOURCE_DIR) / "examples" / name, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    std::string source = text.str();
    source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
    return source;
}

bool hasCode(const model::Snapshot& snapshot, const std::string& code)
{
    return std::any_of(snapshot.diagnostics.begin(), snapshot.diagnostics.end(),
                       [&](const lang::Diagnostic& d) { return d.code == code; });
}

const lang::Diagnostic* firstError(const model::Snapshot& snapshot)
{
    for (const lang::Diagnostic& d : snapshot.diagnostics) {
        if (d.severity == lang::Diagnostic::Severity::Error) {
            return &d;
        }
    }
    return nullptr;
}

constexpr const char* kTwoPosts = R"(param tall = 5 mm

sketch base on XY {
  rect plate center (0 mm, 0 mm) size (80 mm, 30 mm)
}

extrude body = base up 4 mm

sketch posts on body.plate.end {
  circle a center (-20 mm, 0 mm) diameter 16 mm
}

extrude left = posts up 10 mm

sketch other on body.plate.end {
  circle b center (20 mm, 0 mm) diameter 16 mm
}

extrude right = other up tall

sketch mark on top() {
  circle spot center (-20 mm, 0 mm) diameter 4 mm
}

cut dent = mark depth 2 mm
)";

std::string replaced(std::string text, const std::string& from, const std::string& to)
{
    text.replace(text.find(from), from.size(), to);
    return text;
}

TEST(ReviewTest, SpatialPickNeverMovesSilently)
{
    for (const std::string& source : {std::string(kTwoPosts), replaced(kTwoPosts, "on top()", "on left.a.end")}) {
        for (const double tall : {5.0, 20.0}) {
            const model::Snapshot snapshot = rebuild::build(source, {false, {{"tall", tall}}});
            if (lang::hasErrors(snapshot.diagnostics)) {
                continue;
            }
            const int floor = snapshot.faceIndex("dent.spot.floor");
            ASSERT_GE(floor, 0) << "tall " << tall << ": the dent left the left post without an error";
            EXPECT_NEAR(snapshot.faces[floor].centroid.z, 12.0, 1e-6) << "tall " << tall;
        }
    }
}

TEST(ReviewTest, BarePicksAreRejected)
{
    const model::Snapshot bare = rebuild::build(kTwoPosts, {false, {}});
    EXPECT_TRUE(hasCode(bare, "pick"));
    const model::Snapshot wide = rebuild::build(replaced(kTwoPosts, "on top()", "on top(left)"), {false, {}});
    EXPECT_TRUE(hasCode(wide, "pick"));
    const model::Snapshot named = rebuild::build(replaced(kTwoPosts, "on top()", "on left.a.end"), {false, {}});
    EXPECT_FALSE(lang::hasErrors(named.diagnostics));
}

constexpr const char* kHalfCylinder = R"(sketch base on XY {
  circle c center (0 mm, 0 mm) diameter 20 mm
}

extrude body = base up 10 mm

sketch half on XY {
  rect box center (10 mm, 0 mm) size (20 mm, 30 mm)
}

cut k = half through

fillet soft = edge(body.c.wall, k.box.west) radius 1 mm
)";

TEST(ReviewTest, EdgeMeansExactlyOneEdge)
{
    const model::Snapshot snapshot = rebuild::build(kHalfCylinder, {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr) << "edge() silently rounded more than one edge";
    EXPECT_NE(error->hint.find("edges(body.c.wall, k.box.west)"), std::string::npos) << error->hint;
}

std::string splitWith(const std::string& tail)
{
    std::string source = example("split.kd");
    source = source.substr(0, source.find("sketch label"));
    return source + tail;
}

TEST(ReviewTest, MeasurementsOnASplitFaceNeedAll)
{
    const model::Snapshot area = rebuild::build(splitWith("check a: area(body.outline.end) > 0 mm2\n"), {false, {}});
    const lang::Diagnostic* error = firstError(area);
    ASSERT_NE(error, nullptr) << "area() summed two pieces without saying so";
    EXPECT_NE(error->hint.find("all(body.outline.end)"), std::string::npos) << error->hint;

    const model::Snapshot round =
        rebuild::build(splitWith("fillet r = edges(body.outline.end) radius 0.5 mm\n"), {false, {}});
    EXPECT_NE(firstError(round), nullptr) << "edges() rounded the edges of two pieces";
}

TEST(ReviewTest, AllMakesTheSetExplicit)
{
    const model::Snapshot area =
        rebuild::build(splitWith("check a: area(all(body.outline.end)) > 0 mm2\n"), {false, {}});
    EXPECT_FALSE(lang::hasErrors(area.diagnostics));
    ASSERT_EQ(area.checks.size(), 1u);
    EXPECT_TRUE(area.checks.front().passed);
    const model::Snapshot round =
        rebuild::build(splitWith("fillet r = edges(all(body.outline.end)) radius 0.5 mm\n"), {false, {}});
    EXPECT_FALSE(lang::hasErrors(round.diagnostics));
}

TEST(ReviewTest, SeveralFacesNeedAllBeforeTheKernel)
{
    const std::string source = splitWith("");
    for (const std::string& tail :
         {"check a: area(body) > 0 mm2\n", "check d: distance(slotted, body.outline.west) > 0 mm\n",
          "sketch s on body.outline {\n  circle c center (0 mm, 0 mm) diameter 2 mm\n}\n",
          "fillet r = edge(body.outline.west, all(body.outline.end)) radius 1 mm\n"}) {
        const model::Snapshot snapshot = rebuild::build(source + tail, {false, {}});
        EXPECT_TRUE(hasCode(snapshot, "set")) << tail;
    }
}

TEST(ReviewTest, PrintedNamesParse)
{
    for (const std::string& source : {example("bracket.kd"), example("split.kd")}) {
        const model::Snapshot snapshot = rebuild::build(source, {false, {}});
        std::string printed = model::describe(snapshot) + "\n" + model::toJson(snapshot, false).dump();
        for (const lang::Diagnostic& d : snapshot.diagnostics) {
            printed += "\n" + lang::toText(d);
        }
        for (int i = 0; i < static_cast<int>(snapshot.faces.size()); ++i) {
            printed += "\n" + model::describeFace(snapshot, i);
        }
        EXPECT_EQ(printed.find('#'), std::string::npos) << printed;
    }
}

TEST(ReviewTest, PrintedSelectorsResolveToTheirOwnItem)
{
    for (const std::string& source : {example("bracket.kd"), example("split.kd")}) {
        const model::Snapshot snapshot = rebuild::build(source, {false, {}});
        ASSERT_FALSE(snapshot.faces.empty());
        for (int i = 0; i < static_cast<int>(snapshot.faces.size()); ++i) {
            const std::string& selector = snapshot.faces[i].selector;
            const lang::ExpressionResult parsed = lang::parseExpression(selector);
            ASSERT_TRUE(parsed.expr) << selector;
            const model::Selection selection = model::select(*parsed.expr, snapshot);
            EXPECT_FALSE(selection.edges) << selector;
            EXPECT_NE(std::find(selection.items.begin(), selection.items.end(), i), selection.items.end()) << selector;
            if (selector.rfind("all(", 0) != 0) {
                EXPECT_EQ(selection.items.size(), 1u) << selector;
            }
        }
        for (int i = 0; i < static_cast<int>(snapshot.edges.size()); ++i) {
            const std::string& selector = snapshot.edges[i].selector;
            const lang::ExpressionResult parsed = lang::parseExpression(selector);
            ASSERT_TRUE(parsed.expr) << selector;
            const model::Selection selection = model::select(*parsed.expr, snapshot);
            EXPECT_TRUE(selection.edges) << selector;
            EXPECT_NE(std::find(selection.items.begin(), selection.items.end(), i), selection.items.end()) << selector;
            if (selector.rfind("edge(", 0) == 0) {
                EXPECT_EQ(selection.items.size(), 1u) << selector;
            }
        }
    }
}

TEST(ReviewTest, QueriesSeeTheBodyAtAStatement)
{
    const rebuild::Build built = rebuild::run(example("bracket.kd"), {false, {}});
    const lang::ExpressionResult parsed = lang::parseExpression("edge(riser.collar.wall, body.outline.end)");
    ASSERT_TRUE(parsed.expr);
    EXPECT_THROW(model::select(*parsed.expr, built.snapshot), lang::EvalError);
    const model::Snapshot before = rebuild::at(built, "soft", false);
    EXPECT_EQ(model::select(*parsed.expr, before).items.size(), 1u);
    EXPECT_EQ(before.at, "soft");
    EXPECT_LT(before.faceIndex("soft.round"), 0);
    EXPECT_THROW(rebuild::at(built, "nowhere", false), std::invalid_argument);
}

TEST(ReviewTest, SuggestLeavesDerivedParamsAlone)
{
    const std::string source = R"(param width = 40 mm
param inset = width / 16

sketch base on XY {
  rect plate center (0 mm, 0 mm) size (width, 20 mm)
}

extrude body = base up 4 mm

sketch mounts on body.plate.end {
  circle a center (-width / 2 + inset, 0 mm) diameter 3 mm
}

cut holes = mounts through

check margin: distance(holes.a.wall, body.plate.west) >= 2 mm
)";
    const auto suggestions = rebuild::suggest(source);
    ASSERT_FALSE(suggestions.empty());
    for (const auto& s : suggestions) {
        EXPECT_NE(s.param, "inset") << "suggest rewrote the derived param inset as " << s.value;
    }
    EXPECT_TRUE(std::any_of(suggestions.begin(), suggestions.end(), [](const auto& s) { return s.param == "width"; }));
}

TEST(ReviewTest, BuildNeverThrows)
{
    const std::string source = "param b = M" + std::string(400, '9') + "\n";
    model::Snapshot snapshot;
    EXPECT_NO_THROW(snapshot = rebuild::build(source));
    EXPECT_TRUE(lang::hasErrors(snapshot.diagnostics));
}

TEST(ReviewTest, DrillRoundTripsThroughTheTree)
{
    const std::string source = example("bracket.kd");
    const model::Snapshot before = rebuild::build(source, {false, {}});
    const int index = before.faceIndex("body.outline.east");
    ASSERT_GE(index, 0);
    const model::Face& face = before.faces[index];
    const model::Frame frame = model::planeFrame(face.normal, face.centroid);
    const double u = model::dot(face.centroid - frame.origin, frame.x);
    const double v = model::dot(face.centroid - frame.origin, frame.y);
    const std::string drilled = lang::addHole(source, face.selector, u, v, 3.0, 4.0);

    lang::Document document = lang::parse(drilled).document;
    auto& statements = document.statements;
    const auto sketch =
        std::find_if(statements.begin(), statements.end(), [](const auto& s) { return s.name == "hole1"; });
    ASSERT_NE(sketch, statements.end());
    ASSERT_NE(std::next(sketch), statements.end());
    EXPECT_EQ(lang::format(*sketch->plane), "body.outline.east");
    EXPECT_EQ(std::next(sketch)->name, "drill1");
    EXPECT_TRUE(std::all_of(std::next(sketch, 2), statements.end(),
                            [](const auto& s) { return s.kind == lang::Statement::Kind::Check; }));
    statements.erase(sketch, std::next(sketch, 2));
    EXPECT_EQ(lang::format(document), source);

    const model::Snapshot after = rebuild::build(drilled, {false, {}});
    EXPECT_FALSE(lang::hasErrors(after.diagnostics));
    const auto light = [](const model::Snapshot& s) {
        return std::find_if(s.checks.begin(), s.checks.end(), [](const auto& c) { return c.name == "light"; })
            ->lhs.value;
    };
    EXPECT_NEAR(light(after), after.volume, 1e-6) << "the volume check does not see the hole";
    EXPECT_LT(light(after), light(before));
    const int floor = after.faceIndex("drill1.bore.floor");
    ASSERT_GE(floor, 0);
    EXPECT_NEAR(after.faces[floor].centroid.x, face.centroid.x - 4.0, 1e-6);
    EXPECT_NEAR(after.faces[floor].centroid.y, face.centroid.y, 1e-6);
    EXPECT_NEAR(after.faces[floor].centroid.z, face.centroid.z, 1e-6);
    EXPECT_LT(after.volume, before.volume);
    EXPECT_NE(lang::addHole(drilled, face.selector, u, v, 3.0, 4.0).find("sketch hole2 on"), std::string::npos);
}

model::Vec3 compass(const std::string& word)
{
    if (word == "east") {
        return {1, 0, 0};
    }
    if (word == "west") {
        return {-1, 0, 0};
    }
    if (word == "north") {
        return {0, 1, 0};
    }
    if (word == "south") {
        return {0, -1, 0};
    }
    if (word == "top") {
        return {0, 0, 1};
    }
    return {0, 0, -1};
}

TEST(ReviewTest, SideNamesFollowWorldAxes)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"XY", "sketch s on XY {\n  rect r center (5 mm, 3 mm) size (20 mm, 10 mm)\n}\n\nextrude e = s up 4 mm\n"},
        {"XZ", "sketch s on XZ {\n  rect r center (5 mm, 3 mm) size (20 mm, 10 mm)\n}\n\nextrude e = s up 4 mm\n"},
        {"YZ", "sketch s on YZ {\n  rect r center (5 mm, 3 mm) size (20 mm, 10 mm)\n}\n\nextrude e = s up 4 mm\n"},
        {"down",
         "sketch s on XY {\n  rect r center (0 mm, 0 mm) size (40 mm, 30 mm)\n}\n\nextrude e = s up 4 mm\n\n"
         "sketch u on e.r.start {\n  rect r center (5 mm, 3 mm) size (20 mm, 10 mm)\n}\n\nextrude f = u up 4 mm\n"},
    };
    for (const auto& [plane, source] : cases) {
        const model::Snapshot snapshot = rebuild::build(source, {false, {}});
        ASSERT_FALSE(lang::hasErrors(snapshot.diagnostics)) << plane;
        int sides = 0;
        for (const model::Face& face : snapshot.faces) {
            const std::string word = face.name.substr(face.name.rfind('.') + 1);
            if (word == "start" || word == "end") {
                continue;
            }
            ++sides;
            EXPECT_GT(model::dot(face.normal, compass(word)), 0.999) << plane << ": " << face.name;
        }
        EXPECT_GT(sides, 0) << plane;
    }
}

} // namespace
