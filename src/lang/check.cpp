// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lang/check.h"

#include "lang/format.h"
#include "lang/parse.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <utility>

namespace kalve137::lang {

namespace {

struct UnitInfo {
    const char* name;
    double scale;
    Dim dim;
};

constexpr UnitInfo kUnitTable[] = {
    {"mm", 1.0, {1, 0}},
    {"cm", 10.0, {1, 0}},
    {"m", 1000.0, {1, 0}},
    {"in", 25.4, {1, 0}},
    {"deg", std::numbers::pi / 180.0, {0, 1}},
    {"rad", 1.0, {0, 1}},
    {"mm2", 1.0, {2, 0}},
    {"cm2", 100.0, {2, 0}},
    {"mm3", 1.0, {3, 0}},
    {"cm3", 1000.0, {3, 0}},
};

struct ThreadInfo {
    double size;
    double clearance;
    double tap;
};

constexpr ThreadInfo kThreads[] = {
    {2.0, 2.4, 1.6}, {2.5, 2.9, 2.05}, {3.0, 3.4, 2.5},   {4.0, 4.5, 3.3},    {5.0, 5.5, 4.2},
    {6.0, 6.6, 5.0}, {8.0, 9.0, 6.8},  {10.0, 11.0, 8.5}, {12.0, 13.5, 10.2},
};

const UnitInfo& unitInfo(const std::string& unit)
{
    for (const UnitInfo& info : kUnitTable) {
        if (unit == info.name) {
            return info;
        }
    }
    static const UnitInfo kNone{"", 1.0, {0, 0}};
    return kNone;
}

const ThreadInfo* threadInfo(double size)
{
    for (const ThreadInfo& info : kThreads) {
        if (std::abs(info.size - size) < 1e-9) {
            return &info;
        }
    }
    return nullptr;
}

int editDistance(const std::string& a, const std::string& b)
{
    std::vector<int> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) {
        row[j] = static_cast<int>(j);
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const int above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

std::string closest(const std::string& word, const std::vector<std::string>& candidates)
{
    std::string best;
    int bestDistance = 3;
    for (const std::string& candidate : candidates) {
        const int distance = editDistance(word, candidate);
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

bool plainNumber(const Expr& expr)
{
    if (expr.kind == Expr::Kind::Unary) {
        return expr.args[0]->kind == Expr::Kind::Number;
    }
    return expr.kind == Expr::Kind::Number;
}

std::string joined(const std::vector<std::string>& words)
{
    std::string text;
    for (std::size_t i = 0; i < words.size(); ++i) {
        text += (i == 0 ? "" : (i + 1 == words.size() ? " and " : ", ")) + words[i];
    }
    return text;
}

struct Type {
    enum class Kind { Quantity, Thread, Faces, Edges, Plane, Bad };

    enum class Count { One, Several, Set };

    Kind kind = Kind::Bad;
    Dim dim;
    bool anyDim = false;
    bool geometric = false;
    Count count = Count::One;
};

Type faceType(Type::Count count)
{
    Type type{Type::Kind::Faces};
    type.geometric = true;
    type.count = count;
    return type;
}

double threadSize(const std::string& text)
{
    double size = 0.0;
    const char* begin = text.data() + 1;
    const char* end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(begin, end, size);
    return error == std::errc() && ptr == end ? size : -1.0;
}

std::string describe(const Type& type)
{
    switch (type.kind) {
    case Type::Kind::Quantity:
        return type.anyDim ? "a number" : "a " + toString(type.dim);
    case Type::Kind::Thread:
        return "a thread";
    case Type::Kind::Faces:
        return "a face selector";
    case Type::Kind::Edges:
        return "an edge selector";
    case Type::Kind::Plane:
        return "a plane";
    case Type::Kind::Bad:
        return "an invalid value";
    }
    return "?";
}

class Checker {
public:
    explicit Checker(const Document& document) : m_document(document) {}

    std::vector<Diagnostic> run()
    {
        bool sawSolid = false;
        for (const Statement& s : m_document.statements) {
            m_current = &s;
            defineName(s);
            switch (s.kind) {
            case Statement::Kind::Part:
                break;
            case Statement::Kind::Param: {
                const Type type = typeOf(*s.value);
                if (type.geometric) {
                    error(s.value->span, "param-geometry", "A param cannot depend on geometry",
                          "Params feed the geometry, so a measurement here would make a loop. Use a check instead");
                } else if (type.kind != Type::Kind::Quantity && type.kind != Type::Kind::Thread &&
                           type.kind != Type::Kind::Bad) {
                    error(s.value->span, "param-type", "A param holds a number or a thread, not " + describe(type));
                }
                m_params[s.name] = type;
                break;
            }
            case Statement::Kind::Sketch: {
                const Type plane = typeOf(*s.plane);
                if (plane.kind != Type::Kind::Plane && plane.kind != Type::Kind::Faces &&
                    plane.kind != Type::Kind::Bad) {
                    error(s.plane->span, "plane",
                          "A sketch sits on XY, XZ, YZ or a planar face, not " + describe(plane));
                } else if (plane.kind == Type::Kind::Faces && plane.count != Type::Count::One) {
                    error(s.plane->span, "set",
                          "A sketch sits on one face, and '" + format(*s.plane) + "' stands for several",
                          "Name one face, like " + example(*s.plane));
                }
                sketchItems(s);
                break;
            }
            case Statement::Kind::Extrude:
            case Statement::Kind::Cut: {
                const Statement* sketch = m_document.find(s.sketch);
                if (sketch == nullptr || !defined(s.sketch) || sketch->kind != Statement::Kind::Sketch) {
                    error(s.sketchSpan, "unknown-sketch", "'" + s.sketch + "' is not a sketch defined above",
                          suggestion(s.sketch, Statement::Kind::Sketch));
                }
                if (s.kind == Statement::Kind::Cut && !sawSolid) {
                    error(s.span, "cut-first", "Cut '" + s.name + "' has no body to cut",
                          "Extrude a sketch before the first cut");
                }
                if (s.value) {
                    expectLength(*s.value, s.kind == Statement::Kind::Cut ? "cut depth" : "extrude distance");
                }
                sawSolid = true;
                break;
            }
            case Statement::Kind::Fillet: {
                const Type edges = typeOf(*s.lhs);
                if (edges.kind != Type::Kind::Edges && edges.kind != Type::Kind::Bad) {
                    error(s.lhs->span, "fillet-edges", "A fillet rounds edges, not " + describe(edges),
                          "Use edges(face) or edge(face, face)");
                }
                expectLength(*s.value, "fillet radius");
                if (!sawSolid) {
                    error(s.span, "fillet-first", "Fillet '" + s.name + "' has no body to round");
                }
                break;
            }
            case Statement::Kind::Check: {
                const Type left = typeOf(*s.lhs);
                const Type right = typeOf(*s.rhs);
                for (const auto& [side, type] : {std::pair{s.lhs, left}, std::pair{s.rhs, right}}) {
                    if (type.kind != Type::Kind::Quantity && type.kind != Type::Kind::Bad) {
                        error(side->span, "check-type", "A check compares numbers, not " + describe(type));
                    }
                }
                if (left.kind == Type::Kind::Quantity && right.kind == Type::Kind::Quantity && !left.anyDim &&
                    !right.anyDim && !(left.dim == right.dim)) {
                    error(s.rhs->span, "units",
                          "Check '" + s.name + "' compares " + describe(left) + " with " + describe(right),
                          right.dim == Dim{} ? "Add a unit, like " + format(*s.rhs) + " " + unitFor(left.dim)
                                             : std::string());
                }
                break;
            }
            }
            m_defined.push_back(&s);
        }
        return std::move(m_diagnostics);
    }

private:
    void error(Span span, const std::string& code, const std::string& message, const std::string& hint = {})
    {
        m_diagnostics.push_back(
            {Diagnostic::Severity::Error, code, span, m_current ? m_current->name : "", message, hint});
    }

    void sketchItems(const Statement& s)
    {
        if (s.items.empty()) {
            error(s.span, "empty-sketch", "Sketch '" + s.name + "' has no items");
        }
        std::map<std::string, const SketchItem*> named;
        bool solved = false;
        bool loop = false;
        for (const SketchItem& item : s.items) {
            if (item.kind == SketchItem::Kind::Constraint) {
                continue;
            }
            if (named.contains(item.name)) {
                error(item.span, "duplicate", "Sketch '" + s.name + "' already has an item named '" + item.name + "'");
            }
            named[item.name] = &item;
            solved = solved || !item.refs.empty() || item.kind == SketchItem::Kind::Point;
            loop = loop || item.kind == SketchItem::Kind::Line || item.kind == SketchItem::Kind::Arc;
        }
        if (loop && named.contains(s.name)) {
            error(named[s.name]->span, "duplicate",
                  "Item '" + s.name + "' has the name of its sketch, which names the loop of lines and arcs",
                  "Rename the item");
        }
        const auto kindOf = [&](const std::string& name) -> std::optional<SketchItem::Kind> {
            const auto found = named.find(name);
            if (found == named.end()) {
                return std::nullopt;
            }
            return found->second->kind;
        };
        std::set<std::string> pinned;
        std::map<std::string, const SketchItem*> halfPinned;
        for (const SketchItem& item : s.items) {
            if (item.kind != SketchItem::Kind::Constraint || item.call->kind != Expr::Kind::Call ||
                item.call->args.size() != 1 || item.call->args[0]->kind != Expr::Kind::Name) {
                continue;
            }
            const std::string& f = item.call->text;
            const std::string& point = item.call->args[0]->text;
            if (f == "at") {
                pinned.insert(point);
            } else if ((f == "x" || f == "y") && item.value) {
                const auto other = halfPinned.find(point);
                if (other != halfPinned.end() && other->second->call->text != f) {
                    const SketchItem& x = f == "x" ? item : *other->second;
                    const SketchItem& y = f == "x" ? *other->second : item;
                    error(item.span, "constraint", "Point " + point + " is pinned by both x() and y()",
                          "Write at(" + point + ") = (" + format(*x.value) + ", " + format(*y.value) + ")");
                }
                halfPinned[point] = &item;
            }
        }
        for (const SketchItem& item : s.items) {
            switch (item.kind) {
            case SketchItem::Kind::Rect:
                expectLength(*item.center.x, "x of the center");
                expectLength(*item.center.y, "y of the center");
                expectLength(*item.size.x, "width");
                expectLength(*item.size.y, "height");
                break;
            case SketchItem::Kind::Point:
                if (!item.center.x) {
                    if (!pinned.contains(item.name)) {
                        error(item.span, "guess", "Point " + item.name + " has no guess for the solver to start from",
                              "Write point " + item.name +
                                  " near (x, y) with numbers close to where it should land, "
                                  "or pin it with at(" +
                                  item.name + ") = (x, y)");
                    }
                    break;
                }
                for (const ExprPtr& part : {item.center.x, item.center.y}) {
                    if (!plainNumber(*part)) {
                        error(part->span, "guess",
                              "The guess for point " + item.name + " uses '" + format(*part) +
                                  "', but a guess is plain numbers",
                              "Write the guess as numbers, like near (40 mm, 20 mm), and state what the point "
                              "depends on as a constraint, like x(" +
                                  item.name + ") = " + format(*part));
                    }
                }
                expectLength(*item.center.x, "x of the guess");
                expectLength(*item.center.y, "y of the guess");
                break;
            case SketchItem::Kind::Circle:
                if (item.refs.empty()) {
                    expectLength(*item.center.x, "x of the center");
                    expectLength(*item.center.y, "y of the center");
                }
                expectLength(*item.diameter, "diameter");
                [[fallthrough]];
            case SketchItem::Kind::Line:
            case SketchItem::Kind::Arc:
                for (std::size_t i = 0; i < item.refs.size(); ++i) {
                    if (kindOf(item.refs[i]) != SketchItem::Kind::Point) {
                        error(item.refSpans[i], "sketch-ref",
                              "'" + item.refs[i] + "' is not a point of sketch '" + s.name + "'",
                              "Add it, like point " + item.refs[i] + " near (0 mm, 0 mm)");
                    }
                }
                if (item.refs.size() >= 2 && item.refs[0] == item.refs[1]) {
                    error(item.span, "sketch-ref", "'" + item.name + "' starts and ends at the same point");
                }
                break;
            case SketchItem::Kind::Constraint:
                if (!solved) {
                    error(item.span, "constraint",
                          "Sketch '" + s.name + "' has no points, lines or arcs for a constraint to hold",
                          "Rect and circle with a written center are already fixed by their numbers");
                }
                constraint(s, item, kindOf);
                break;
            }
        }
    }

    struct Signature {
        std::vector<std::vector<SketchItem::Kind>> refs;
        Dim value;
        bool dimension = false;
        std::string text;
    };

    static std::vector<Signature> signatures(const std::string& f)
    {
        using K = SketchItem::Kind;
        const std::vector<K> curve{K::Line, K::Arc, K::Circle};
        const std::vector<K> round{K::Arc, K::Circle};
        const Dim length{1, 0};
        if (f == "at") {
            return {{{{K::Point}}, length, true, "at(point) = (0 mm, 0 mm)"}};
        }
        if (f == "x" || f == "y") {
            return {{{{K::Point}}, length, true, f + "(point) = 0 mm"}};
        }
        if (f == "coincident") {
            return {
                {{{K::Point}, {K::Point, K::Line, K::Arc, K::Circle}}, {}, false, "coincident(point, point or curve)"}};
        }
        if (f == "horizontal" || f == "vertical") {
            return {{{{K::Line}}, {}, false, f + "(line)"},
                    {{{K::Point}, {K::Point}}, {}, false, f + "(point, point)"}};
        }
        if (f == "parallel" || f == "perpendicular") {
            return {{{{K::Line}, {K::Line}}, {}, false, f + "(line, line)"}};
        }
        if (f == "equal") {
            return {{{{K::Line}, {K::Line}}, {}, false, "equal(line, line)"},
                    {{round, round}, {}, false, "equal(arc or circle, arc or circle)"}};
        }
        if (f == "tangent") {
            return {{{{K::Line}, round}, {}, false, "tangent(line, arc or circle)"},
                    {{round, curve}, {}, false, "tangent(arc or circle, curve)"}};
        }
        if (f == "distance") {
            return {{{{K::Point}, {K::Point}}, length, true, "distance(point, point) = 10 mm"},
                    {{{K::Point}, {K::Line}}, length, true, "distance(point, line) = 10 mm"}};
        }
        if (f == "length") {
            return {{{{K::Line}}, length, true, "length(line) = 10 mm"}};
        }
        if (f == "radius") {
            return {{{round}, length, true, "radius(arc or circle) = 5 mm"}};
        }
        if (f == "angle") {
            return {{{{K::Line}, {K::Line}}, {0, 1}, true, "angle(line, line) = 30 deg"}};
        }
        return {};
    }

    template <typename KindOf>
    void constraint(const Statement& s, const SketchItem& item, const KindOf& kindOf)
    {
        const Expr& call = *item.call;
        if (call.kind != Expr::Kind::Call) {
            error(item.span, "constraint", "Expected a constraint like horizontal(base)");
            return;
        }
        if (call.text == "fix") {
            std::string where = "(x, y)";
            const std::optional<SketchItem::Kind> kind = call.args.size() == 1 && call.args[0]->kind == Expr::Kind::Name
                                                             ? kindOf(call.args[0]->text)
                                                             : std::nullopt;
            if (kind == SketchItem::Kind::Point) {
                const SketchItem& point = named(s, call.args[0]->text);
                if (point.center.x) {
                    where = "(" + format(*point.center.x) + ", " + format(*point.center.y) + ")";
                }
            }
            const std::string target = call.args.empty() ? "p" : format(*call.args[0]);
            error(call.span, "constraint", "fix() is gone. A point is pinned by a constraint that states where it is",
                  "Write at(" + target + ") = " + where);
            return;
        }
        const std::vector<Signature> forms = signatures(call.text);
        if (forms.empty()) {
            const std::string best =
                closest(call.text, {"at", "x", "y", "coincident", "horizontal", "vertical", "parallel", "perpendicular",
                                    "equal", "tangent", "distance", "length", "radius", "angle"});
            error(call.span, "constraint", "Unknown constraint '" + call.text + "'",
                  best.empty() ? "Constraints are at, x, y, coincident, horizontal, vertical, parallel, "
                                 "perpendicular, equal, tangent, distance, length, radius and angle"
                               : "Did you mean '" + best + "'?");
            return;
        }
        std::vector<std::optional<SketchItem::Kind>> kinds;
        for (const ExprPtr& arg : call.args) {
            const std::optional<SketchItem::Kind> found =
                arg->kind == Expr::Kind::Name ? kindOf(arg->text) : std::nullopt;
            if (!found) {
                error(arg->span, "sketch-ref",
                      "'" + format(*arg) + "' is not a point, line, arc or circle of sketch '" + s.name + "'");
                return;
            }
            const SketchItem::Kind kind = *found;
            if (kind == SketchItem::Kind::Rect ||
                (kind == SketchItem::Kind::Circle && named(s, arg->text).refs.empty())) {
                error(arg->span, "sketch-ref",
                      "'" + arg->text + "' is fixed by its written numbers, so a constraint cannot move it",
                      "Give it a named center point to let constraints move it");
                return;
            }
            kinds.push_back(kind);
        }
        const Signature* match = nullptr;
        std::string formsText;
        for (const Signature& form : forms) {
            formsText += (formsText.empty() ? "" : " or ") + form.text;
            if (form.refs.size() != kinds.size()) {
                continue;
            }
            bool fits = true;
            for (std::size_t i = 0; i < kinds.size(); ++i) {
                fits = fits && std::find(form.refs[i].begin(), form.refs[i].end(), *kinds[i]) != form.refs[i].end();
            }
            if (fits && !match) {
                match = &form;
            }
        }
        if (match == nullptr) {
            std::string got;
            for (std::size_t i = 0; i < kinds.size(); ++i) {
                got += (i == 0 ? "" : ", ") + call.args[i]->text + " is " + kindWord(*kinds[i]);
            }
            error(call.span, "constraint", "'" + format(item) + "' does not fit " + call.text + "(): " + got,
                  "Write it as " + formsText);
            return;
        }
        if (call.text == "at") {
            if (!item.center.x) {
                error(item.span, "constraint", "at() pins a point and needs its position",
                      "Write it as " + match->text);
                return;
            }
            for (const ExprPtr& part : {item.center.x, item.center.y}) {
                expectLength(*part, "position in at()");
                if (typeOf(*part).geometric) {
                    error(part->span, "geometry-input", "A pin cannot depend on a measurement");
                }
            }
            return;
        }
        if (match->dimension && !item.value) {
            error(item.span, "constraint", call.text + "() is a dimension and needs a value",
                  "Write it as " + match->text);
        } else if (!match->dimension && item.value) {
            error(item.value->span, "constraint", call.text + "() takes no value", "Write it as " + match->text);
        } else if (item.value) {
            const Type type = typeOf(*item.value);
            if (type.kind == Type::Kind::Quantity && !type.anyDim && !(type.dim == match->value)) {
                error(item.value->span, "units",
                      call.text + "() needs " + (match->value == Dim{0, 1} ? "an angle" : "a length") + ", not " +
                          describe(type),
                      "Write it as " + match->text);
            } else if (type.kind != Type::Kind::Quantity && type.kind != Type::Kind::Bad) {
                error(item.value->span, "type", call.text + "() needs a number, not " + describe(type));
            }
            if (type.geometric) {
                error(item.value->span, "geometry-input", "A dimension cannot depend on a measurement");
            }
        }
    }

    static const SketchItem& named(const Statement& s, const std::string& name)
    {
        for (const SketchItem& item : s.items) {
            if (item.kind != SketchItem::Kind::Constraint && item.name == name) {
                return item;
            }
        }
        throw std::logic_error("no sketch item " + name);
    }

    static std::string kindWord(SketchItem::Kind kind)
    {
        switch (kind) {
        case SketchItem::Kind::Rect:
            return "a rect";
        case SketchItem::Kind::Circle:
            return "a circle";
        case SketchItem::Kind::Point:
            return "a point";
        case SketchItem::Kind::Line:
            return "a line";
        case SketchItem::Kind::Arc:
            return "an arc";
        case SketchItem::Kind::Constraint:
            return "a constraint";
        }
        return "?";
    }

    static std::string unitFor(Dim dim)
    {
        if (dim == Dim{1, 0}) {
            return "mm";
        }
        if (dim == Dim{2, 0}) {
            return "mm2";
        }
        if (dim == Dim{3, 0}) {
            return "mm3";
        }
        if (dim == Dim{0, 1}) {
            return "deg";
        }
        return "";
    }

    bool defined(const std::string& name) const
    {
        return std::any_of(m_defined.begin(), m_defined.end(), [&](const Statement* s) { return s->name == name; });
    }

    const Statement* definedStatement(const std::string& name) const
    {
        for (const Statement* s : m_defined) {
            if (s->name == name) {
                return s;
            }
        }
        return nullptr;
    }

    std::string suggestion(const std::string& name, std::optional<Statement::Kind> kind) const
    {
        std::vector<std::string> candidates;
        for (const Statement* s : m_defined) {
            if (!kind || s->kind == *kind) {
                candidates.push_back(s->name);
            }
        }
        const std::string best = closest(name, candidates);
        if (!best.empty()) {
            return "Did you mean '" + best + "'?";
        }
        const Statement* later = m_document.find(name);
        if (later != nullptr) {
            return "'" + name + "' is defined further down. Move it above this line";
        }
        return {};
    }

    void defineName(const Statement& s)
    {
        if (s.name.empty()) {
            return;
        }
        if (defined(s.name)) {
            error(s.span, "duplicate", "'" + s.name + "' is already defined", "Every statement needs its own name");
        }
        if (isThreadName(s.name) || isPlaneName(s.name)) {
            error(s.span, "reserved", "'" + s.name + "' reads as a thread or plane and cannot be a name");
        }
    }

    void expectLength(const Expr& expr, const std::string& what)
    {
        const Type type = typeOf(expr);
        if (type.kind == Type::Kind::Bad) {
            return;
        }
        if (type.kind != Type::Kind::Quantity) {
            error(expr.span, "type", "The " + what + " needs a length, not " + describe(type));
        } else if (!type.anyDim && !(type.dim == Dim{1, 0})) {
            const bool bare = type.dim == Dim{};
            std::string hint;
            if (bare && expr.kind == Expr::Kind::Name) {
                hint = "Param " + expr.text + " has no unit. Add one where it is defined, like 10 mm";
            } else if (bare) {
                hint = "Write " + format(expr) + " mm";
            }
            error(expr.span, "units", "The " + what + " needs a length, not " + describe(type), hint);
        }
        if (type.geometric) {
            error(expr.span, "geometry-input", "The " + what + " cannot depend on a measurement",
                  "Feed geometry from params and hold measurements in checks");
        }
    }

    Type selectorPath(const Expr& expr)
    {
        const Statement* feature = definedStatement(expr.path[0]);
        if (feature == nullptr) {
            error(expr.span, "unknown-name", "'" + expr.path[0] + "' is not a feature defined above",
                  suggestion(expr.path[0], std::nullopt));
            return {};
        }
        if (feature->kind != Statement::Kind::Extrude && feature->kind != Statement::Kind::Cut &&
            feature->kind != Statement::Kind::Fillet) {
            error(expr.span, "not-feature",
                  "'" + expr.path[0] + "' is a " + toString(feature->kind) +
                      ", and only extrude, cut and fillet make faces");
            return {};
        }
        if (expr.path.size() == 1) {
            return faceType(Type::Count::Several);
        }
        if (feature->kind == Statement::Kind::Fillet) {
            if (expr.path[1] != "round" || expr.path.size() > 2) {
                error(expr.span, "selector", "A fillet only has the faces '" + expr.path[0] + ".round'");
                return {};
            }
            return faceType(Type::Count::One);
        }
        const Statement* sketch = sketchOf(m_document, *feature);
        if (sketch == nullptr) {
            return {};
        }
        const std::vector<std::string> itemNames = faceItems(*sketch);
        if (std::find(itemNames.begin(), itemNames.end(), expr.path[1]) == itemNames.end()) {
            const std::vector<std::string> loopSides = faceParts(m_document, *feature, sketch->name);
            if (std::find(loopSides.begin(), loopSides.end(), expr.path[1]) != loopSides.end()) {
                error(expr.span, "selector",
                      "'" + expr.path[1] + "' is a side of the loop of sketch '" + sketch->name + "', not an item",
                      "Write " + expr.path[0] + "." + sketch->name + "." + expr.path[1]);
                return {};
            }
            const std::string best = closest(expr.path[1], itemNames);
            error(expr.span, "selector", "Sketch '" + sketch->name + "' has no item '" + expr.path[1] + "'",
                  best.empty() ? "Items are " + joined(itemNames) : "Did you mean '" + best + "'?");
            return {};
        }
        if (expr.path.size() == 2) {
            return faceType(Type::Count::Several);
        }
        const std::vector<std::string> parts = faceParts(m_document, *feature, expr.path[1]);
        if (expr.path.size() > 3 || std::find(parts.begin(), parts.end(), expr.path[2]) == parts.end()) {
            error(expr.span, "selector",
                  "'" + format(expr) + "' names no face. " + expr.path[0] + "." + expr.path[1] + " has " +
                      joined(parts),
                  closest(expr.path[2], parts).empty() ? "" : "Did you mean '" + closest(expr.path[2], parts) + "'?");
            return {};
        }
        return faceType(Type::Count::One);
    }

    std::string example(const Expr& expr) const
    {
        if (expr.kind == Expr::Kind::Call && !expr.args.empty()) {
            return example(*expr.args[0]);
        }
        const std::string head = expr.kind == Expr::Kind::Name ? expr.text : (expr.path.empty() ? "" : expr.path[0]);
        const Statement* feature = m_document.find(head);
        const Statement* sketch = feature != nullptr ? sketchOf(m_document, *feature) : nullptr;
        const std::vector<std::string> items = sketch != nullptr ? faceItems(*sketch) : std::vector<std::string>();
        if (items.empty()) {
            return head + ".item.end";
        }
        std::string item = items.front();
        if (expr.path.size() >= 2 && std::find(items.begin(), items.end(), expr.path[1]) != items.end()) {
            item = expr.path[1];
        }
        const std::vector<std::string> parts = faceParts(m_document, *feature, item);
        const bool hasEnd = std::find(parts.begin(), parts.end(), "end") != parts.end();
        return head + "." + item + "." + (hasEnd ? std::string("end") : parts.front());
    }

    void expectFaces(const Expr& expr, bool setAllowed, const std::string& use)
    {
        expectFaces(expr, typeOf(expr), setAllowed, use);
    }

    void expectFaces(const Expr& expr, const Type& type, bool setAllowed, const std::string& use)
    {
        if (type.kind == Type::Kind::Bad) {
            return;
        }
        if (type.kind != Type::Kind::Faces) {
            error(expr.span, "type", use + " needs a face, not " + describe(type));
        } else if (type.count == Type::Count::Several) {
            error(expr.span, "set", "'" + format(expr) + "' stands for several faces, and " + use + " needs one",
                  setAllowed
                      ? "Name one face, like " + example(expr) + ", or say all(" + format(expr) + ") to mean every one"
                      : "Name one face, like " + example(expr));
        } else if (type.count == Type::Count::Set && !setAllowed) {
            error(expr.span, "set", use + " needs one face, and '" + format(expr) + "' is a set",
                  "Name one face, like " + example(expr));
        }
    }

    Type typeOf(const Expr& expr)
    {
        switch (expr.kind) {
        case Expr::Kind::Number: {
            Type type{Type::Kind::Quantity};
            if (expr.unit == kPieces) {
                error(expr.span, "pick", "'" + format(expr) + "' only counts the pieces a pick chooses among",
                      "Use it as the second argument of a pick, like east(body.outline.end, 2 pieces)");
                return {};
            }
            if (expr.unit.empty()) {
                type.anyDim = expr.number == 0.0;
            } else {
                type.dim = unitInfo(expr.unit).dim;
            }
            return type;
        }
        case Expr::Kind::Thread: {
            const double size = threadSize(expr.text);
            if (threadInfo(size) == nullptr) {
                error(expr.span, "thread", "No table entry for thread " + expr.text,
                      "Known threads are M2, M2.5, M3, M4, M5, M6, M8, M10 and M12");
            }
            return {Type::Kind::Thread};
        }
        case Expr::Kind::Name: {
            if (isPlaneName(expr.text)) {
                return {Type::Kind::Plane};
            }
            const auto param = m_params.find(expr.text);
            if (param != m_params.end()) {
                return param->second;
            }
            if (const Statement* s = definedStatement(expr.text)) {
                if (s->kind == Statement::Kind::Extrude || s->kind == Statement::Kind::Cut ||
                    s->kind == Statement::Kind::Fillet) {
                    return faceType(Type::Count::Several);
                }
                error(expr.span, "not-value", "'" + expr.text + "' is a " + toString(s->kind) + ", not a value");
                return {};
            }
            error(expr.span, "unknown-name", "Unknown name '" + expr.text + "'", suggestion(expr.text, std::nullopt));
            return {};
        }
        case Expr::Kind::Path:
            return selectorPath(expr);
        case Expr::Kind::Call:
            return call(expr);
        case Expr::Kind::Unary: {
            Type inner = typeOf(*expr.args[0]);
            if (inner.kind != Type::Kind::Quantity && inner.kind != Type::Kind::Bad) {
                error(expr.span, "type", "Cannot negate " + describe(inner));
                return {};
            }
            return inner;
        }
        case Expr::Kind::Binary: {
            const Type left = typeOf(*expr.args[0]);
            const Type right = typeOf(*expr.args[1]);
            if (left.kind == Type::Kind::Bad || right.kind == Type::Kind::Bad) {
                return {};
            }
            if (left.kind != Type::Kind::Quantity || right.kind != Type::Kind::Quantity) {
                error(expr.span, "type",
                      std::string("Cannot apply '") + expr.op + "' to " + describe(left) + " and " + describe(right));
                return {};
            }
            Type result{Type::Kind::Quantity};
            result.geometric = left.geometric || right.geometric;
            if (expr.op == '+' || expr.op == '-') {
                if (!left.anyDim && !right.anyDim && !(left.dim == right.dim)) {
                    const bool bare = left.dim == Dim{} || right.dim == Dim{};
                    error(expr.span, "units",
                          std::string("Cannot ") + (expr.op == '+' ? "add " : "subtract ") + describe(right) +
                              (expr.op == '+' ? " to " : " from ") + describe(left),
                          bare ? "A bare number has no unit. Add one, like 2 mm" : std::string());
                    return {};
                }
                result.dim = left.anyDim ? right.dim : left.dim;
                result.anyDim = left.anyDim && right.anyDim;
                return result;
            }
            const int sign = expr.op == '*' ? 1 : -1;
            result.dim = {left.dim.length + sign * right.dim.length, left.dim.angle + sign * right.dim.angle};
            result.anyDim = left.anyDim && right.anyDim;
            return result;
        }
        }
        return {};
    }

    Type call(const Expr& expr)
    {
        const std::string& f = expr.text;
        auto arity = [&](std::size_t count) {
            if (expr.args.size() != count) {
                error(expr.span, "arity",
                      f + "() takes " + std::to_string(count) + " argument" + (count == 1 ? "" : "s") + ", not " +
                          std::to_string(expr.args.size()));
                return false;
            }
            return true;
        };
        auto expect = [&](std::size_t index, std::initializer_list<Type::Kind> kinds, const std::string& what) {
            const Type type = typeOf(*expr.args[index]);
            if (type.kind != Type::Kind::Bad && std::find(kinds.begin(), kinds.end(), type.kind) == kinds.end()) {
                error(expr.args[index]->span, "type", f + "() needs " + what + ", not " + describe(type));
            }
        };
        Type length{Type::Kind::Quantity, {1, 0}};
        if (f == "clearance" || f == "tap") {
            if (arity(1)) {
                expect(0, {Type::Kind::Thread}, "a thread like M4");
            }
            return length;
        }
        Type measured = length;
        measured.geometric = true;
        if (isFaceFunction(f)) {
            if (expr.args.empty()) {
                std::string like = "body.outline.end";
                for (const Statement* s : m_defined) {
                    if (s->kind == Statement::Kind::Extrude) {
                        Expr name;
                        name.kind = Expr::Kind::Name;
                        name.text = s->name;
                        like = example(name);
                    }
                }
                error(expr.span, "pick", f + "() needs the face to pick a piece of",
                      "Name the face, like " + like +
                          ". A pick only chooses among the pieces of one named face, "
                          "like " +
                          f + "(" + like + ", 2 pieces), so a param change can never move it to another face");
                return {};
            }
            if (expr.args.size() > 2) {
                error(expr.span, "arity", f + "() takes a face and the number of its pieces");
                return {};
            }
            const Expr& among = *expr.args[0];
            const Type type = typeOf(among);
            if (type.kind != Type::Kind::Bad && (type.kind != Type::Kind::Faces || type.count != Type::Count::One)) {
                error(among.span, "pick",
                      f + "() picks among the pieces of one named face, and '" + format(among) + "' is not one name",
                      "Name the face in full, like " + example(among));
            }
            if (expr.args.size() == 2) {
                const Expr& count = *expr.args[1];
                if (count.kind != Expr::Kind::Number || count.unit != kPieces || count.number < 2.0 ||
                    count.number != std::floor(count.number)) {
                    error(count.span, "pick",
                          f + "() needs the number of pieces it chooses among as a whole number of 2 or more, not '" +
                              format(count) + "'",
                          "Write it like " + f + "(" + format(among) + ", 2 pieces)");
                }
            }
            return faceType(Type::Count::One);
        }
        if (f == "all") {
            if (arity(1)) {
                expect(0, {Type::Kind::Faces}, "faces");
            }
            return faceType(Type::Count::Set);
        }
        Type edges{Type::Kind::Edges};
        edges.geometric = true;
        if (f == "edges") {
            if (expr.args.empty() || expr.args.size() > 2) {
                error(expr.span, "arity", "edges() takes one face, or two faces for the edges they share");
                return {};
            }
            for (const ExprPtr& arg : expr.args) {
                expectFaces(*arg, true, "edges()");
            }
            return edges;
        }
        if (f == "edge") {
            if (arity(2)) {
                expectFaces(*expr.args[0], false, "edge()");
                expectFaces(*expr.args[1], false, "edge()");
            }
            return edges;
        }
        if (f == "volume") {
            arity(0);
            measured.dim = {3, 0};
            return measured;
        }
        if (f == "area") {
            if (arity(1)) {
                expectFaces(*expr.args[0], true, "area()");
            }
            measured.dim = {2, 0};
            return measured;
        }
        if (f == "distance") {
            if (arity(2)) {
                for (const ExprPtr& arg : expr.args) {
                    const Type type = typeOf(*arg);
                    if (type.kind != Type::Kind::Edges) {
                        expectFaces(*arg, type, true, "distance()");
                    }
                }
            }
            return measured;
        }
        if (f == "size_x" || f == "size_y" || f == "size_z") {
            arity(0);
            return measured;
        }
        if (f == "count") {
            if (arity(1)) {
                expect(0, {Type::Kind::Faces, Type::Kind::Edges}, "faces or edges");
            }
            measured.dim = {};
            return measured;
        }
        const std::string best =
            closest(f, {"clearance", "tap", "top", "bottom", "east", "west", "north", "south", "all", "edges", "edge",
                        "volume", "area", "distance", "size_x", "size_y", "size_z", "count"});
        error(expr.span, "unknown-function", "Unknown function '" + f + "'",
              best.empty() ? "" : "Did you mean '" + best + "'?");
        return {};
    }

    const Document& m_document;
    const Statement* m_current = nullptr;
    std::vector<const Statement*> m_defined;
    std::map<std::string, Type> m_params;
    std::vector<Diagnostic> m_diagnostics;
};

} // namespace

std::string toString(Dim dim)
{
    if (dim == Dim{}) {
        return "plain number";
    }
    if (dim == Dim{1, 0}) {
        return "length";
    }
    if (dim == Dim{2, 0}) {
        return "area";
    }
    if (dim == Dim{3, 0}) {
        return "volume";
    }
    if (dim == Dim{0, 1}) {
        return "angle";
    }
    return "length^" + std::to_string(dim.length) + " angle^" + std::to_string(dim.angle);
}

std::string toText(const Quantity& quantity)
{
    if (quantity.thread) {
        return "M" + formatNumber(quantity.value);
    }
    const double rounded = std::round(quantity.value * 1000.0) / 1000.0;
    std::string text = formatNumber(rounded == 0.0 ? 0.0 : rounded);
    if (quantity.dim == Dim{1, 0}) {
        return text + " mm";
    }
    if (quantity.dim == Dim{2, 0}) {
        return text + " mm2";
    }
    if (quantity.dim == Dim{3, 0}) {
        return text + " mm3";
    }
    if (quantity.dim == Dim{0, 1}) {
        return formatNumber(std::round(quantity.value * 180.0 / std::numbers::pi * 1000.0) / 1000.0) + " deg";
    }
    return text;
}

bool isMeasurement(const std::string& function)
{
    return function == "volume" || function == "area" || function == "distance" || function == "size_x" ||
           function == "size_y" || function == "size_z" || function == "count";
}

bool isFaceFunction(const std::string& function)
{
    return function == "top" || function == "bottom" || function == "east" || function == "west" ||
           function == "north" || function == "south";
}

bool isPlaneName(const std::string& word)
{
    return word == "XY" || word == "XZ" || word == "YZ";
}

std::vector<std::string> sideWords(const Document& document, const Statement& feature)
{
    const Statement* sketch = sketchOf(document, feature);
    const std::string plane =
        sketch != nullptr && sketch->plane && sketch->plane->kind == Expr::Kind::Name ? sketch->plane->text : "";
    if (plane == "XY") {
        return {"east", "west", "north", "south"};
    }
    if (plane == "XZ") {
        return {"east", "west", "top", "bottom"};
    }
    if (plane == "YZ") {
        return {"north", "south", "top", "bottom"};
    }
    return {"east", "west", "north", "south", "top", "bottom"};
}

bool hasLoop(const Statement& sketch)
{
    return std::any_of(sketch.items.begin(), sketch.items.end(), [](const SketchItem& item) {
        return item.kind == SketchItem::Kind::Line || item.kind == SketchItem::Kind::Arc;
    });
}

std::vector<std::string> faceItems(const Statement& sketch)
{
    std::vector<std::string> items;
    for (const SketchItem& item : sketch.items) {
        if (item.kind == SketchItem::Kind::Rect || item.kind == SketchItem::Kind::Circle) {
            items.push_back(item.name);
        }
    }
    if (hasLoop(sketch)) {
        items.push_back(sketch.name);
    }
    return items;
}

std::vector<std::string> faceParts(const Document& document, const Statement& feature, const std::string& item)
{
    const Statement* sketch = sketchOf(document, feature);
    if (sketch == nullptr) {
        return {};
    }
    std::vector<std::string> parts;
    if (feature.kind == Statement::Kind::Extrude) {
        parts = {"start", "end"};
    } else if (feature.kind == Statement::Kind::Cut && !feature.through) {
        parts = {"floor"};
    }
    if (item == sketch->name && hasLoop(*sketch)) {
        for (const SketchItem& side : sketch->items) {
            if (side.kind == SketchItem::Kind::Line || side.kind == SketchItem::Kind::Arc) {
                parts.push_back(side.name);
            }
        }
        return parts;
    }
    for (const SketchItem& candidate : sketch->items) {
        if (candidate.name != item || candidate.kind == SketchItem::Kind::Constraint) {
            continue;
        }
        if (candidate.kind == SketchItem::Kind::Rect) {
            const std::vector<std::string> sides = sideWords(document, feature);
            parts.insert(parts.end(), sides.begin(), sides.end());
        } else if (candidate.kind == SketchItem::Kind::Circle) {
            parts.push_back("wall");
        } else {
            return {};
        }
        return parts;
    }
    return {};
}

const Statement* sketchOf(const Document& document, const Statement& feature)
{
    const Statement* sketch = document.find(feature.sketch);
    return sketch != nullptr && sketch->kind == Statement::Kind::Sketch ? sketch : nullptr;
}

double clearanceDiameter(double thread)
{
    const ThreadInfo* info = threadInfo(thread);
    return info != nullptr ? info->clearance : 0.0;
}

double tapDiameter(double thread)
{
    const ThreadInfo* info = threadInfo(thread);
    return info != nullptr ? info->tap : 0.0;
}

std::vector<Diagnostic> check(const Document& document)
{
    return Checker(document).run();
}

Quantity evaluate(const Expr& expr, const Params& params, Measurer* measurer)
{
    switch (expr.kind) {
    case Expr::Kind::Number: {
        const UnitInfo& info = unitInfo(expr.unit);
        return {expr.number * info.scale, info.dim};
    }
    case Expr::Kind::Thread:
        if (threadInfo(threadSize(expr.text)) == nullptr) {
            throw EvalError(expr.span, "No table entry for thread " + expr.text);
        }
        return {threadSize(expr.text), {}, true};
    case Expr::Kind::Name: {
        const auto found = params.find(expr.text);
        if (found == params.end()) {
            throw EvalError(expr.span, "'" + expr.text + "' has no value here");
        }
        return found->second;
    }
    case Expr::Kind::Path:
        throw EvalError(expr.span, "'" + format(expr) + "' selects faces and has no value");
    case Expr::Kind::Call: {
        if (expr.text == "clearance" || expr.text == "tap") {
            const Quantity thread = evaluate(*expr.args.at(0), params, measurer);
            const double value = expr.text == "clearance" ? clearanceDiameter(thread.value) : tapDiameter(thread.value);
            if (!thread.thread || value <= 0.0) {
                throw EvalError(expr.span, expr.text + "() needs a known thread");
            }
            return {value, {1, 0}};
        }
        if (isMeasurement(expr.text)) {
            if (measurer == nullptr) {
                throw EvalError(expr.span, expr.text + "() needs built geometry");
            }
            return measurer->measure(expr, params);
        }
        throw EvalError(expr.span, expr.text + "() selects geometry and has no value");
    }
    case Expr::Kind::Unary: {
        Quantity inner = evaluate(*expr.args[0], params, measurer);
        inner.value = -inner.value;
        return inner;
    }
    case Expr::Kind::Binary: {
        const Quantity a = evaluate(*expr.args[0], params, measurer);
        const Quantity b = evaluate(*expr.args[1], params, measurer);
        switch (expr.op) {
        case '+':
            return {a.value + b.value, a.dim == Dim{} ? b.dim : a.dim};
        case '-':
            return {a.value - b.value, a.dim == Dim{} ? b.dim : a.dim};
        case '*':
            return {a.value * b.value, {a.dim.length + b.dim.length, a.dim.angle + b.dim.angle}};
        case '/':
            if (b.value == 0.0) {
                throw EvalError(expr.span, "Division by zero");
            }
            return {a.value / b.value, {a.dim.length - b.dim.length, a.dim.angle - b.dim.angle}};
        default:
            break;
        }
        break;
    }
    }
    throw EvalError(expr.span, "Cannot evaluate this expression");
}

} // namespace kalve137::lang
