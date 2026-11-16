// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "model/select.h"

#include "lang/format.h"
#include "model/compass.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace kalve137::model {

namespace {

constexpr double kDirectionTolerance = 0.999;
constexpr double kLevelTolerance = 1e-6;
constexpr const char* kSides[] = {"top", "bottom", "east", "west", "north", "south"};
constexpr const char* kAtHint = "If a later statement used it up, query the body that statement saw with "
                                "kalvec query FILE --at STATEMENT";

enum class Use { Any, OneOrSet, One };

bool matchesPath(const std::string& name, const std::string& path)
{
    return name == path ||
           (name.size() > path.size() && name.compare(0, path.size(), path) == 0 && name[path.size()] == '.');
}

bool isEdgeCall(const lang::Expr& expr)
{
    return expr.kind == lang::Expr::Kind::Call && (expr.text == "edges" || expr.text == "edge");
}

std::vector<int> extremeAmong(const Snapshot& snapshot, const std::vector<int>& faces, Vec3 direction)
{
    std::vector<int> result;
    double best = -1e300;
    for (const int i : faces) {
        const double level = dot(snapshot.faces[i].centroid, direction);
        if (level > best + kLevelTolerance) {
            best = level;
            result = {i};
        } else if (std::abs(level - best) <= kLevelTolerance) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<int> namedGroup(const Snapshot& snapshot, const std::string& name)
{
    std::vector<int> group;
    for (int i = 0; i < static_cast<int>(snapshot.faces.size()); ++i) {
        if (snapshot.faces[i].name == name) {
            group.push_back(i);
        }
    }
    return group;
}

std::string pick(const std::string& side, const std::string& face, std::size_t pieces)
{
    return side + "(" + face + ", " + std::to_string(pieces) + " " + lang::kPieces + ")";
}

std::vector<std::string> pieceSelectors(const Snapshot& snapshot, const std::vector<int>& group, int face)
{
    std::vector<std::string> result;
    for (const char* side : kSides) {
        const std::vector<int> picked = extremeAmong(snapshot, group, compassDirection(side));
        if (picked.size() == 1 && picked.front() == face) {
            result.push_back(pick(side, snapshot.faces[face].name, group.size()));
        }
    }
    return result;
}

std::vector<std::string> everyPiece(const Snapshot& snapshot, const std::vector<int>& group)
{
    std::vector<std::string> picks;
    for (const int face : group) {
        const std::vector<std::string> ways = pieceSelectors(snapshot, group, face);
        if (!ways.empty()) {
            picks.push_back(ways.front());
        }
    }
    return picks;
}

[[noreturn]] void pickError(lang::Span span, const std::string& message, const std::string& hint)
{
    throw lang::EvalError(span, message, hint, "pick");
}

std::string piecesText(std::size_t count)
{
    return count == 1 ? std::string("one face") : std::to_string(count) + " " + lang::kPieces;
}

std::string orList(const std::vector<std::string>& words)
{
    std::string text;
    for (std::size_t i = 0; i < words.size(); ++i) {
        text += (i == 0 ? "" : (i + 1 == words.size() ? " or " : ", ")) + words[i];
    }
    return text;
}

std::string selectorList(const Snapshot& snapshot, const std::vector<int>& faces, std::size_t limit = 6)
{
    std::vector<std::string> unique;
    for (const int i : faces) {
        const std::string& selector =
            snapshot.faces[i].selector.empty() ? snapshot.faces[i].name : snapshot.faces[i].selector;
        if (std::find(unique.begin(), unique.end(), selector) == unique.end()) {
            unique.push_back(selector);
        }
    }
    std::string text;
    for (std::size_t i = 0; i < unique.size() && i < limit; ++i) {
        text += (i == 0 ? "" : ", ") + unique[i];
    }
    if (unique.size() > limit) {
        text += " and " + std::to_string(unique.size() - limit) + " more";
    }
    return text;
}

[[noreturn]] void notFound(const lang::Expr& expr, const Snapshot& snapshot, const std::string& path)
{
    const std::string head = path.substr(0, path.find('.'));
    std::vector<int> related;
    for (int i = 0; i < static_cast<int>(snapshot.faces.size()); ++i) {
        if (matchesPath(snapshot.faces[i].name, head)) {
            related.push_back(i);
        }
    }
    const std::string hint =
        related.empty() ? std::string(kAtHint)
                        : "Faces of " + head + " here are " + selectorList(snapshot, related, 10) + ". " + kAtHint;
    throw lang::EvalError(expr.span, "'" + path + "' matches no face of the body at this point", hint);
}

Selection resolveFaces(const lang::Expr& expr, const Snapshot& snapshot, Use use, const std::string& purpose)
{
    if (expr.kind == lang::Expr::Kind::Path || expr.kind == lang::Expr::Kind::Name) {
        const std::string path = lang::format(expr);
        Selection selection;
        std::set<std::string> names;
        for (int i = 0; i < static_cast<int>(snapshot.faces.size()); ++i) {
            if (matchesPath(snapshot.faces[i].name, path)) {
                selection.items.push_back(i);
                names.insert(snapshot.faces[i].name);
            }
        }
        if (selection.items.empty()) {
            notFound(expr, snapshot, path);
        }
        if (use == Use::Any || selection.items.size() == 1) {
            return selection;
        }
        const std::string orAll = use == Use::OneOrSet ? ", or say all(" + path + ") to mean every one" : "";
        if (names.size() > 1) {
            throw lang::EvalError(expr.span,
                                  purpose + " needs one face, and '" + path + "' stands for " +
                                      std::to_string(selection.items.size()) + " faces",
                                  "Name one face, like " + *names.begin() + orAll);
        }
        const std::vector<std::string> picks = everyPiece(snapshot, selection.items);
        std::string hint = picks.empty() ? "No pick tells the pieces apart" : "Pick one piece with " + orList(picks);
        if (use == Use::OneOrSet) {
            hint += ", or say all(" + path + ") to mean every piece";
        }
        throw lang::EvalError(expr.span,
                              purpose + " needs one face, and '" + path + "' is split into " +
                                  std::to_string(selection.items.size()) + " pieces",
                              hint);
    }
    if (expr.kind == lang::Expr::Kind::Call && expr.text == "all" && expr.args.size() == 1) {
        if (use == Use::One) {
            throw lang::EvalError(expr.span, purpose + " needs one face, and '" + lang::format(expr) + "' is a set");
        }
        return resolveFaces(*expr.args[0], snapshot, Use::Any, purpose);
    }
    if (expr.kind == lang::Expr::Kind::Call && lang::isFaceFunction(expr.text) &&
        (expr.args.size() == 1 || expr.args.size() == 2)) {
        const Selection among = resolveFaces(*expr.args[0], snapshot, Use::Any, purpose);
        std::set<std::string> names;
        for (const int i : among.items) {
            names.insert(snapshot.faces[i].name);
        }
        const std::string face = lang::format(*expr.args[0]);
        if (names.size() > 1) {
            pickError(expr.span,
                      expr.text + "() picks among the pieces of one face, and '" + face + "' stands for " +
                          std::to_string(names.size()) + " faces",
                      "Name the face in full, like " + *names.begin());
        }
        const std::size_t actual = among.items.size();
        const std::string fixAll =
            actual == 1 ? "Name it as " + face
                        : "Check which piece is meant, then write one of " + orList(everyPiece(snapshot, among.items));
        if (expr.args.size() == 1) {
            pickError(expr.span,
                      "'" + lang::format(expr) + "' does not say how many pieces of " + face + " it chooses among",
                      actual == 1 ? face + " is one face here. " + fixAll
                                  : "Write it as " + pick(expr.text, face, actual) +
                                        ", so a change that adds or removes a piece stops here instead of picking "
                                        "another one");
        }
        const double stated = expr.args[1]->number;
        if (stated != static_cast<double>(actual)) {
            pickError(expr.span,
                      "'" + lang::format(expr) + "' assumed " + lang::format(*expr.args[1]) + ", and " + face +
                          " is now " + piecesText(actual),
                      fixAll);
        }
        Selection selection;
        selection.items = extremeAmong(snapshot, among.items, compassDirection(expr.text));
        if (selection.items.size() != 1) {
            throw lang::EvalError(expr.span,
                                  "'" + lang::format(expr) + "' ties between " +
                                      std::to_string(selection.items.size()) + " pieces",
                                  "Use another direction. The pieces are " + selectorList(snapshot, among.items));
        }
        return selection;
    }
    if (isEdgeCall(expr)) {
        throw lang::EvalError(expr.span, purpose + " needs a face, and '" + lang::format(expr) + "' selects edges");
    }
    throw lang::EvalError(expr.span, "'" + lang::format(expr) + "' is not a selector");
}

std::vector<int> sharedEdges(const Snapshot& snapshot, const std::vector<int>& a, const std::vector<int>& b)
{
    const std::set<int> left(a.begin(), a.end());
    const std::set<int> right(b.begin(), b.end());
    std::vector<int> result;
    for (int i = 0; i < static_cast<int>(snapshot.edges.size()); ++i) {
        const Edge& e = snapshot.edges[i];
        if ((left.count(e.faceA) != 0 && right.count(e.faceB) != 0) ||
            (left.count(e.faceB) != 0 && right.count(e.faceA) != 0)) {
            result.push_back(i);
        }
    }
    return result;
}

Selection resolveEdges(const lang::Expr& expr, const Snapshot& snapshot, const std::string& purpose)
{
    Selection selection{true, {}};
    const std::string text = lang::format(expr);
    if (expr.text == "edges" && expr.args.size() == 1) {
        const Selection faces = resolveFaces(*expr.args[0], snapshot, Use::OneOrSet, "edges()");
        const std::set<int> wanted(faces.items.begin(), faces.items.end());
        for (int i = 0; i < static_cast<int>(snapshot.edges.size()); ++i) {
            if (wanted.count(snapshot.edges[i].faceA) != 0 || wanted.count(snapshot.edges[i].faceB) != 0) {
                selection.items.push_back(i);
            }
        }
        if (selection.items.empty()) {
            throw lang::EvalError(expr.span, "'" + text + "' matches no edge");
        }
        return selection;
    }
    if (expr.args.size() != 2) {
        throw lang::EvalError(expr.span, "'" + text + "' needs two faces");
    }
    const Use use = expr.text == "edge" ? Use::One : Use::OneOrSet;
    const Selection a = resolveFaces(*expr.args[0], snapshot, use, expr.text + "()");
    const Selection b = resolveFaces(*expr.args[1], snapshot, use, expr.text + "()");
    selection.items = sharedEdges(snapshot, a.items, b.items);
    if (selection.items.empty()) {
        throw lang::EvalError(expr.span, "'" + text + "' matches no edge. The faces do not touch in this body",
                              kAtHint);
    }
    if (expr.text == "edge" && selection.items.size() > 1) {
        const std::string set = "edges(" + lang::format(*expr.args[0]) + ", " + lang::format(*expr.args[1]) + ")";
        throw lang::EvalError(expr.span,
                              purpose + " got " + std::to_string(selection.items.size()) + " edges from '" + text +
                                  "', which means exactly one",
                              "Say " + set + " to use all " + std::to_string(selection.items.size()));
    }
    return selection;
}

std::string fixed(double value, int digits = 2)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(digits);
    out << (std::abs(value) < 0.5 * std::pow(10.0, -digits) ? 0.0 : value);
    std::string text = out.str();
    if (text.find('.') != std::string::npos) {
        while (text.back() == '0') {
            text.pop_back();
        }
        if (text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

std::string axisWord(Vec3 v)
{
    const std::tuple<Vec3, const char*> axes[] = {{{1, 0, 0}, "+X"},  {{-1, 0, 0}, "-X"}, {{0, 1, 0}, "+Y"},
                                                  {{0, -1, 0}, "-Y"}, {{0, 0, 1}, "+Z"},  {{0, 0, -1}, "-Z"}};
    for (const auto& [axis, word] : axes) {
        if (dot(v, axis) > kDirectionTolerance) {
            return word;
        }
    }
    return "(" + fixed(v.x, 3) + ", " + fixed(v.y, 3) + ", " + fixed(v.z, 3) + ")";
}

} // namespace

int Snapshot::faceIndex(const std::string& selector) const
{
    for (int i = 0; i < static_cast<int>(faces.size()); ++i) {
        if (faces[i].selector == selector) {
            return i;
        }
    }
    return -1;
}

Selection select(const lang::Expr& expr, const Snapshot& snapshot)
{
    return isEdgeCall(expr) ? resolveEdges(expr, snapshot, "The query")
                            : resolveFaces(expr, snapshot, Use::Any, "The query");
}

Selection selectUse(const lang::Expr& expr, const Snapshot& snapshot, const std::string& purpose)
{
    return isEdgeCall(expr) ? resolveEdges(expr, snapshot, purpose)
                            : resolveFaces(expr, snapshot, Use::OneOrSet, purpose);
}

int selectOneFace(const lang::Expr& expr, const Snapshot& snapshot, const std::string& purpose)
{
    return resolveFaces(expr, snapshot, Use::One, purpose).items.front();
}

std::string selectorFor(const Snapshot& snapshot, int face)
{
    const Face& f = snapshot.faces.at(face);
    const std::vector<int> group = namedGroup(snapshot, f.name);
    if (group.size() == 1) {
        return f.name;
    }
    const std::vector<std::string> pieces = pieceSelectors(snapshot, group, face);
    return pieces.empty() ? "all(" + f.name + ")" : pieces.front();
}

void assignSelectors(Snapshot& snapshot)
{
    for (int i = 0; i < static_cast<int>(snapshot.faces.size()); ++i) {
        snapshot.faces[i].selector = selectorFor(snapshot, i);
    }
    std::map<std::pair<int, int>, int> pairs;
    for (const Edge& edge : snapshot.edges) {
        ++pairs[std::minmax(edge.faceA, edge.faceB)];
    }
    for (Edge& edge : snapshot.edges) {
        const bool seam = edge.faceB < 0 || edge.faceB == edge.faceA;
        std::string a = edge.faceA >= 0 ? snapshot.faces[edge.faceA].selector : "";
        std::string b = seam ? "" : snapshot.faces[edge.faceB].selector;
        if (seam) {
            edge.selector = "edges(" + a + ")";
            continue;
        }
        if (b < a) {
            std::swap(a, b);
        }
        const bool single =
            pairs[std::minmax(edge.faceA, edge.faceB)] == 1 && a.rfind("all(", 0) != 0 && b.rfind("all(", 0) != 0;
        edge.selector = single ? "edge(" : "edges(";
        edge.selector += a;
        edge.selector += ", ";
        edge.selector += b;
        edge.selector += ")";
    }
}

std::string describeFace(const Snapshot& snapshot, int index)
{
    const Face& face = snapshot.faces.at(index);
    std::ostringstream out;
    out << face.selector << ": ";
    if (face.kind == "plane") {
        out << "flat face facing " << axisWord(face.normal);
    } else if (face.kind == "cylinder") {
        out << "cylindrical face, diameter " << fixed(face.radius * 2.0) << " mm, axis " << axisWord(face.axis);
    } else {
        out << face.kind << " face";
    }
    out << ", area " << fixed(face.area) << " mm2, center (" << fixed(face.centroid.x) << ", " << fixed(face.centroid.y)
        << ", " << fixed(face.centroid.z) << ")";
    out << ", made by " << face.madeBy;
    if (face.selector.rfind("all(", 0) == 0) {
        out << ". It is one of " << namedGroup(snapshot, face.name).size() << " pieces of " << face.name
            << " that no direction tells apart";
    }
    std::vector<std::string> neighbors;
    for (const Edge& edge : snapshot.edges) {
        const int other = edge.faceA == index ? edge.faceB : (edge.faceB == index ? edge.faceA : -1);
        if (other >= 0 && other != index) {
            const std::string& selector = snapshot.faces[other].selector;
            if (std::find(neighbors.begin(), neighbors.end(), selector) == neighbors.end()) {
                neighbors.push_back(selector);
            }
        }
    }
    std::sort(neighbors.begin(), neighbors.end());
    out << ". Touches " << neighbors.size() << " face" << (neighbors.size() == 1 ? "" : "s");
    if (!neighbors.empty()) {
        out << ":";
        for (const std::string& selector : neighbors) {
            out << " " << selector;
        }
    }
    return out.str();
}

std::string describeAxes(const Frame& frame)
{
    return "first coordinate along " + axisWord(frame.x) + ", second along " + axisWord(frame.y) + ", origin at (" +
           fixed(frame.origin.x) + ", " + fixed(frame.origin.y) + ", " + fixed(frame.origin.z) + ")";
}

std::string describe(const Snapshot& snapshot)
{
    std::ostringstream out;
    out << "Part " << (snapshot.part.empty() ? "(unnamed)" : snapshot.part);
    if (!snapshot.at.empty()) {
        out << " as statement " << snapshot.at << " sees it";
    }
    if (!snapshot.built) {
        out << " has no body yet.";
        return out.str();
    }
    const Vec3 size = snapshot.boxMax - snapshot.boxMin;
    out << " is one solid of " << fixed(snapshot.volume, 1) << " mm3 in a " << fixed(size.x) << " x " << fixed(size.y)
        << " x " << fixed(size.z) << " mm box.";
    std::map<std::string, int> kinds;
    for (const Face& face : snapshot.faces) {
        ++kinds[face.kind];
    }
    out << " It has " << snapshot.faces.size() << " faces (";
    bool first = true;
    for (const auto& [kind, count] : kinds) {
        out << (first ? "" : ", ") << count << " " << kind;
        first = false;
    }
    out << ") and " << snapshot.edges.size() << " edges.";
    std::map<std::string, int> holes;
    for (const Face& face : snapshot.faces) {
        if (face.kind == "cylinder" && face.name.size() > 5 &&
            face.name.compare(face.name.size() - 5, 5, ".wall") == 0) {
            ++holes[fixed(face.radius * 2.0) + " mm along " + axisWord(face.axis)];
        }
    }
    for (const auto& [what, count] : holes) {
        out << " " << count << " round " << (count == 1 ? "hole or boss" : "holes or bosses") << " of " << what << ".";
    }
    if (!snapshot.at.empty()) {
        return out.str();
    }
    int passed = 0;
    for (const CheckResult& check : snapshot.checks) {
        passed += check.passed ? 1 : 0;
    }
    out << " Checks: " << passed << " of " << snapshot.checks.size() << " pass.";
    for (const CheckResult& check : snapshot.checks) {
        out << "\n  " << (check.passed ? "pass " : "FAIL ") << check.name << ": " << check.expression << " is "
            << lang::toText(check.lhs) << ", needs " << check.comparison << " " << lang::toText(check.rhs);
    }
    for (const SketchPlacement& placement : snapshot.placements) {
        out << "\n  sketch " << placement.sketch << " on " << placement.on << ": " << describeAxes(placement.frame);
    }
    if (!snapshot.complete) {
        out << "\n  The build stopped early. See the diagnostics.";
    }
    return out.str();
}

} // namespace kalve137::model
