// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lang/check.h"
#include "lang/edit.h"
#include "lang/format.h"
#include "lang/parse.h"
#include "lang/reference.h"
#include "look/raster.h"
#include "model/select.h"
#include "rebuild/rebuild.h"
#include "rebuild/suggest.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
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

std::vector<std::string> codes(const std::vector<lang::Diagnostic>& diagnostics)
{
    std::vector<std::string> result;
    result.reserve(diagnostics.size());
    for (const auto& d : diagnostics) {
        result.push_back(d.code);
    }
    return result;
}

std::vector<lang::Diagnostic> frontEnd(const std::string& source)
{
    lang::ParseResult parsed = lang::parse(source);
    if (lang::hasErrors(parsed.diagnostics)) {
        return parsed.diagnostics;
    }
    return lang::check(parsed.document);
}

std::set<std::string> faceIds(const model::Snapshot& snapshot)
{
    std::set<std::string> ids;
    for (const auto& face : snapshot.faces) {
        ids.insert(face.selector);
    }
    return ids;
}

TEST(FormatTest, FormattingTwiceGivesTheSameText)
{
    const std::string messy = "part   p\nparam a=2mm+3 mm*2\nparam b = (a - 1 mm) - (a - 2 mm)\n"
                              "sketch s on XY {\n rect r center (0mm,0 mm) size (a,  b+ 1 mm)\n}\n"
                              "extrude e = s up -(-a)\ncheck c: volume() <= 1 cm3\n";
    const std::string once = lang::format(lang::parse(messy).document);
    const std::string twice = lang::format(lang::parse(once).document);
    EXPECT_EQ(once, twice);
    EXPECT_NE(once.find("param a = 2 mm + 3 mm * 2"), std::string::npos);
    EXPECT_NE(once.find("param b = a - 1 mm - (a - 2 mm)"), std::string::npos);
}

TEST(FormatTest, ExamplesAreCanonical)
{
    for (const char* name : {"bracket.kd", "split.kd"}) {
        const std::string source = example(name);
        EXPECT_EQ(lang::format(lang::parse(source).document), source) << name;
    }
}

TEST(ParseTest, CommentsAreRejectedWithAReason)
{
    const auto parsed = lang::parse("param a = 1 mm # why\n");
    ASSERT_EQ(codes(parsed.diagnostics), std::vector<std::string>{"comment"});
}

TEST(ParseTest, KeywordsStayUsableAsNames)
{
    EXPECT_TRUE(frontEnd("param depth = 3 mm\nparam size = depth * 2\n").empty());
}

TEST(CheckTest, CatchesUnitsBeforeTheKernel)
{
    const auto diagnostics =
        frontEnd("param a = 10\nsketch s on XY {\n  rect r center (0 mm, 0 mm) size (a, 5 mm)\n}\n");
    ASSERT_EQ(codes(diagnostics), std::vector<std::string>{"units"});
    EXPECT_EQ(diagnostics[0].hint, "Param a has no unit. Add one where it is defined, like 10 mm");
}

TEST(CheckTest, ParamsCannotReadGeometry)
{
    const auto diagnostics = frontEnd("sketch s on XY {\n  rect r center (0 mm, 0 mm) size (5 mm, 5 mm)\n}\n"
                                      "extrude e = s up 2 mm\nparam v = volume()\n");
    ASSERT_EQ(codes(diagnostics), std::vector<std::string>{"param-geometry"});
}

TEST(CheckTest, SelectorTyposGetASuggestion)
{
    std::string source = example("bracket.kd");
    source.replace(source.find("bolts.left.wall"), 15, "bolts.left.wal");
    const auto diagnostics = frontEnd(source);
    ASSERT_EQ(codes(diagnostics), std::vector<std::string>{"selector"});
    EXPECT_EQ(diagnostics[0].hint, "Did you mean 'wall'?");
}

TEST(CheckTest, ForwardReferencesPointAtTheLaterLine)
{
    const auto diagnostics = frontEnd("param a = b\nparam b = 1 mm\n");
    ASSERT_EQ(codes(diagnostics), std::vector<std::string>{"unknown-name"});
    EXPECT_NE(diagnostics[0].hint.find("further down"), std::string::npos);
}

TEST(BuildTest, BracketBuildsWithNamedFacesAndPassingChecks)
{
    const model::Snapshot snapshot = rebuild::build(example("bracket.kd"));
    ASSERT_TRUE(snapshot.complete);
    EXPECT_FALSE(lang::hasErrors(snapshot.diagnostics));
    EXPECT_EQ(snapshot.checks.size(), 5u);
    EXPECT_NEAR(snapshot.volume, 11928.19, 0.1);
    for (const char* id :
         {"body.outline.end", "riser.collar.wall", "bolts.left.wall", "tapped.bore.floor", "soft.round"}) {
        EXPECT_GE(snapshot.faceIndex(id), 0) << id;
    }
}

TEST(BuildTest, SameSourceGivesTheSameSnapshot)
{
    const model::Snapshot a = rebuild::build(example("bracket.kd"));
    const model::Snapshot b = rebuild::build(example("bracket.kd"));
    ASSERT_EQ(a.faces.size(), b.faces.size());
    for (std::size_t i = 0; i < a.faces.size(); ++i) {
        EXPECT_EQ(a.faces[i].selector, b.faces[i].selector);
        EXPECT_EQ(a.faces[i].triangles, b.faces[i].triangles);
    }
    const look::Frame fa = look::render(a, {}, 200, 150);
    const look::Frame fb = look::render(b, {}, 200, 150);
    EXPECT_EQ(fa.pixels, fb.pixels);
}

TEST(BuildTest, FaceIdsSurviveParamSweeps)
{
    const std::string source = example("bracket.kd");
    const std::set<std::string> reference = faceIds(rebuild::build(source, {false, {}}));
    for (const double rise : {7.5, 20.0}) {
        for (const double boss : {10.0, 18.0}) {
            for (const double edge : {6.0, 9.0}) {
                const model::Snapshot snapshot =
                    rebuild::build(source, {false, {{"rise", rise}, {"boss", boss}, {"edge", edge}}});
                EXPECT_EQ(faceIds(snapshot), reference) << "rise " << rise << " boss " << boss << " edge " << edge;
            }
        }
    }
}

TEST(BuildTest, BlindHoleCheckCatchesABreakthrough)
{
    const model::Snapshot snapshot = rebuild::build(example("bracket.kd"), {false, {{"rise", 4.0}}});
    const auto blind =
        std::find_if(snapshot.checks.begin(), snapshot.checks.end(), [](const auto& c) { return c.name == "blind"; });
    ASSERT_NE(blind, snapshot.checks.end());
    EXPECT_FALSE(blind->passed);
    EXPECT_EQ(blind->lhs.value, 0.0);
    EXPECT_LT(snapshot.faceIndex("tapped.bore.floor"), 0);
}

TEST(BuildTest, SplitFaceFailsLoudlyAndNamesThePieces)
{
    const model::Snapshot snapshot = rebuild::build(example("split.kd"));
    EXPECT_FALSE(snapshot.complete);
    const auto found = std::find_if(snapshot.diagnostics.begin(), snapshot.diagnostics.end(),
                                    [](const auto& d) { return d.code == "build"; });
    ASSERT_NE(found, snapshot.diagnostics.end());
    EXPECT_EQ(found->hint, "Pick one piece with east(body.outline.end, 2 pieces) or west(body.outline.end, 2 pieces)");
}

TEST(BuildTest, FailedCheckGetsAProvenParamFix)
{
    std::string source = example("bracket.kd");
    source = lang::setParam(source, "edge", "4 mm");
    const model::Snapshot broken = rebuild::build(source, {false, {}});
    ASSERT_FALSE(broken.checks.empty());
    EXPECT_FALSE(std::all_of(broken.checks.begin(), broken.checks.end(), [](const auto& c) { return c.passed; }));
    const auto suggestions = rebuild::suggest(source);
    ASSERT_FALSE(suggestions.empty());
    EXPECT_TRUE(suggestions.front().allChecksPass);
    const model::Snapshot fixed = rebuild::build(suggestions.front().source, {false, {}});
    EXPECT_TRUE(std::all_of(fixed.checks.begin(), fixed.checks.end(), [](const auto& c) { return c.passed; }));
}

TEST(EditTest, SetParamRewritesCanonically)
{
    const std::string source = example("bracket.kd");
    const std::string edited = lang::setParam(source, "rise", "12 mm");
    EXPECT_EQ(lang::setParam(edited, "rise", "10 mm"), source);
}

TEST(BuildTest, ACutThatMissesStopsWithTheAxesOfItsFace)
{
    const std::string source = R"(part block

sketch base on XY {
  rect outline center (0 mm, 0 mm) size (40 mm, 20 mm)
}

extrude body = base up 10 mm

sketch side on body.outline.west {
  circle hit center (5 mm, 5 mm) diameter 4 mm
  circle high center (-5 mm, 30 mm) diameter 4 mm
}

cut holes = side depth 5 mm
)";
    const model::Snapshot missed = rebuild::build(source, {false, {}});
    const auto error = std::find_if(missed.diagnostics.begin(), missed.diagnostics.end(),
                                    [](const auto& d) { return d.code == "miss"; });
    ASSERT_NE(error, missed.diagnostics.end());
    EXPECT_EQ(error->message,
              "Cut 'holes' removes nothing where 'high' of sketch 'side' is, so it lies outside the body");
    EXPECT_EQ(error->hint, "Sketch 'side' on body.outline.west has its first coordinate along -Y, second along +Z, "
                           "origin at (-20, 0, 0). kalvec describe FILE lists every sketch on a face");

    std::string fixed = source;
    fixed.replace(fixed.find("30 mm"), 5, "5 mm");
    const model::Snapshot built = rebuild::build(fixed, {false, {}});
    EXPECT_TRUE(built.complete);
    EXPECT_NE(model::describe(built).find("\n  sketch side on body.outline.west: first coordinate along -Y, second "
                                          "along +Z, origin at (-20, 0, 0)"),
              std::string::npos)
        << model::describe(built);
}

bool hasWord(const std::string& text, const std::string& word, char next = 0)
{
    const auto letter = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
        const std::size_t end = at + word.size();
        const bool before = at == 0 || !letter(text[at - 1]);
        const bool after =
            next != 0 ? end < text.size() && text[end] == next : end == text.size() || !letter(text[end]);
        if (before && after) {
            return true;
        }
    }
    return false;
}

TEST(ReferenceTest, EveryKeywordUnitAndConstraintIsOnTheCard)
{
    const std::string card = lang::reference();
    for (const std::string& word : lang::keywords()) {
        EXPECT_TRUE(hasWord(card, word)) << word;
    }
    for (const std::string& unit : lang::units()) {
        EXPECT_TRUE(hasWord(card, unit)) << unit;
    }
    const lang::ParseResult parsed =
        lang::parse("part p\n\nsketch s on XY {\n  point a near (0 mm, 0 mm)\n  qqqqqq(a)\n}\n");
    ASSERT_TRUE(parsed.diagnostics.empty());
    const std::vector<lang::Diagnostic> diagnostics = lang::check(parsed.document);
    const auto unknown =
        std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& d) { return d.code == "constraint"; });
    ASSERT_NE(unknown, diagnostics.end());
    const std::string prefix = "Constraints are ";
    ASSERT_EQ(unknown->hint.rfind(prefix, 0), 0u) << unknown->hint;
    std::string names = unknown->hint.substr(prefix.size());
    std::replace(names.begin(), names.end(), ',', ' ');
    std::istringstream words(names);
    int count = 0;
    for (std::string name; words >> name;) {
        if (name != "and") {
            EXPECT_TRUE(hasWord(card, name, '(')) << name;
            ++count;
        }
    }
    EXPECT_EQ(count, 14);
}

} // namespace
