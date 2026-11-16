// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lang/format.h"
#include "lang/parse.h"
#include "model/select.h"
#include "rebuild/rebuild.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace {

using namespace kalve137;

const lang::Diagnostic* firstError(const model::Snapshot& snapshot)
{
    for (const lang::Diagnostic& d : snapshot.diagnostics) {
        if (d.severity == lang::Diagnostic::Severity::Error) {
            return &d;
        }
    }
    return nullptr;
}

std::string replaced(std::string text, const std::string& from, const std::string& to)
{
    text.replace(text.find(from), from.size(), to);
    return text;
}

constexpr const char* kSlotted = R"(param second = 38 mm

sketch base on XY {
  rect plate center (0 mm, 0 mm) size (80 mm, 30 mm)
}

extrude body = base up 8 mm

sketch slots on body.plate.end {
  rect middle center (0 mm, 0 mm) size (10 mm, 40 mm)
  rect other center (second, 0 mm) size (6 mm, 40 mm)
}

cut slotted = slots depth 3 mm

sketch mark on east(body.plate.end, 2 pieces) {
  circle spot center (30 mm, 0 mm) diameter 4 mm
}

cut dent = mark depth 1 mm
)";

TEST(PickGuardTest, PickHoldsWhileThePieceCountHolds)
{
    const model::Snapshot snapshot = rebuild::build(kSlotted, {false, {}});
    ASSERT_EQ(firstError(snapshot), nullptr) << firstError(snapshot)->message;
    const int floor = snapshot.faceIndex("dent.spot.floor");
    ASSERT_GE(floor, 0);
    EXPECT_GT(snapshot.faces[floor].centroid.x, 5.0);
}

TEST(PickGuardTest, PickFailsLoudlyWhenAPieceIsAdded)
{
    const model::Snapshot snapshot = rebuild::build(kSlotted, {false, {{"second", 20.0}}});
    EXPECT_LT(snapshot.faceIndex("dent.spot.floor"), 0) << "the pick chose among three pieces without an error";
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "pick");
    EXPECT_EQ(error->statement, "mark");
    EXPECT_NE(error->message.find("2 pieces"), std::string::npos) << error->message;
    EXPECT_NE(error->message.find("3 pieces"), std::string::npos) << error->message;
    EXPECT_NE(error->hint.find("east(body.plate.end, 3 pieces)"), std::string::npos) << error->hint;
}

TEST(PickGuardTest, PickWithoutACountIsWrittenInFullByTheFix)
{
    const model::Snapshot snapshot =
        rebuild::build(replaced(kSlotted, "east(body.plate.end, 2 pieces)", "east(body.plate.end)"), {false, {}});
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "pick");
    EXPECT_NE(error->hint.find("east(body.plate.end, 2 pieces)"), std::string::npos) << error->hint;
}

TEST(PickGuardTest, PrintedSelectorsCarryTheCount)
{
    const model::Snapshot snapshot = rebuild::build(kSlotted, {false, {}});
    std::vector<std::string> picks;
    for (const model::Face& face : snapshot.faces) {
        if (face.name == "body.plate.end") {
            picks.push_back(face.selector);
        }
    }
    std::sort(picks.begin(), picks.end());
    EXPECT_EQ(picks, (std::vector<std::string>{"east(body.plate.end, 2 pieces)", "west(body.plate.end, 2 pieces)"}));
    for (const std::string& pick : picks) {
        const lang::ExprPtr expr = lang::parseExpression(pick).expr;
        ASSERT_NE(expr, nullptr) << pick;
        EXPECT_EQ(lang::format(*expr), pick);
        EXPECT_EQ(model::select(*expr, snapshot).items.size(), 1U) << pick;
    }
}

TEST(PickGuardTest, CountsOutsideAPickAreRejected)
{
    const model::Snapshot snapshot = rebuild::build(replaced(kSlotted, "param second = 38 mm", "param n = 2 pieces"));
    const lang::Diagnostic* error = firstError(snapshot);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "pick");
}

} // namespace
