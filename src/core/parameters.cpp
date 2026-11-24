// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/parameters.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <memory>
#include <numbers>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kalve137::core {

namespace {

constexpr std::array<std::string_view, 24> kKeywords{
    "part", "param", "sketch",   "on",      "rect", "circle", "center", "size", "point", "line",  "arc",     "near",
    "from", "to",    "diameter", "extrude", "cut",  "fillet", "check",  "up",   "down",  "depth", "through", "radius"};
constexpr std::array<std::string_view, 15> kFeatureKinds{"sketch", "extrude", "revolve", "fillet", "chamfer",
                                                         "shell",  "pattern", "import",  "plane",  "offset_face",
                                                         "hole",   "move",    "axis",    "point",  "appearance"};
constexpr std::array<std::string_view, 5> kPlainOnlyFunctions{"sqrt", "sin", "cos", "tan", "pi"};
constexpr std::array<std::string_view, 2> kPlainOnlyUnits{"um", "ft"};
constexpr std::string_view kPlainOnlyMessage = "sqrt, sin, cos, tan, pi and ^ work only in a value without names";
constexpr std::string_view kPlainOnlyHint = "Work that part out as a number or put it in its own parameter";
constexpr std::string_view kPieces = "pieces";
constexpr std::string_view kPiecesMessage = " only counts the pieces a pick chooses among";
constexpr std::string_view kPiecesHint = "Use a plain number here, like 4";
constexpr std::string_view kUnreadableMessage = "Can't read this as a value";
constexpr std::string_view kUnreadableHint = "Type a number or an expression, such as 30/2 or 1in";

constexpr double kDegreesPerRadian = 180.0 / std::numbers::pi;

struct UnitInfo {
    std::string_view name;
    double scale;
    Dim dim;
};

constexpr UnitInfo kUnitTable[] = {
    {"mm", 1.0, {1, 0}},     {"cm", 10.0, {1, 0}},    {"m", 1000.0, {1, 0}},
    {"in", 25.4, {1, 0}},    {"deg", 1.0, {0, 1}},    {"rad", kDegreesPerRadian, {0, 1}},
    {"mm2", 1.0, {2, 0}},    {"cm2", 100.0, {2, 0}},  {"mm3", 1.0, {3, 0}},
    {"cm3", 1000.0, {3, 0}}, {"pieces", 1.0, {0, 0}},
};

constexpr int kMaxDecimals = 40;

const UnitInfo& unitInfo(std::string_view unit)
{
    for (const UnitInfo& info : kUnitTable) {
        if (unit == info.name) {
            return info;
        }
    }
    static const UnitInfo kNone{"", 1.0, {0, 0}};
    return kNone;
}

bool isUnit(std::string_view word)
{
    return std::any_of(std::begin(kUnitTable), std::end(kUnitTable),
                       [word](const UnitInfo& info) { return info.name == word; });
}

char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool sameWord(std::string_view a, std::string_view b)
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}

template <std::size_t N>
bool listsWord(const std::array<std::string_view, N>& words, std::string_view word)
{
    return std::any_of(words.begin(), words.end(), [word](std::string_view entry) { return sameWord(entry, word); });
}

bool isPlainOnlyFunction(std::string_view word)
{
    return listsWord(kPlainOnlyFunctions, word);
}

bool isPlainOnlyWord(std::string_view word)
{
    return isPlainOnlyFunction(word) || listsWord(kPlainOnlyUnits, word);
}

bool lists(std::span<const std::string_view> words, std::string_view word)
{
    return std::find(words.begin(), words.end(), word) != words.end();
}

std::string withArticle(const std::string& noun)
{
    const bool vowel = !noun.empty() && std::string_view("aeiou").find(noun.front()) != std::string_view::npos;
    return (vowel ? "an " : "a ") + noun;
}

bool isThreadName(std::string_view word)
{
    if (word.size() < 2 || word.front() != 'M') {
        return false;
    }
    std::size_t i = 1;
    while (i < word.size() && std::isdigit(static_cast<unsigned char>(word[i])) != 0) {
        ++i;
    }
    if (i == 1) {
        return false;
    }
    if (i == word.size()) {
        return true;
    }
    if (word[i] != '.' || i + 1 == word.size()) {
        return false;
    }
    for (++i; i < word.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(word[i])) == 0) {
            return false;
        }
    }
    return true;
}

bool isPlaneName(std::string_view word)
{
    return word == "XY" || word == "XZ" || word == "YZ";
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool identStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool identChar(char c)
{
    return identStart(c) || isDigit(c);
}

std::optional<double> parseNumber(const std::string& text)
{
    double value = 0.0;
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    if (!(stream >> value) || stream.peek() != std::istringstream::traits_type::eof()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> fixedText(double value)
{
    if (value == 0.0) {
        return "0";
    }
    for (int precision = 0; precision <= kMaxDecimals; ++precision) {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::fixed << std::setprecision(precision) << value;
        const std::optional<double> back = parseNumber(stream.str());
        if (back && *back == value) {
            return stream.str();
        }
    }
    return std::nullopt;
}

std::string unitFor(Dim dim)
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
    return {};
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

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Expr {
    enum class Kind { Number, Name, Path, Call, Unary, Binary };

    Kind kind = Kind::Number;
    double number = 0.0;
    std::string unit;
    std::string text;
    std::vector<std::string> path;
    char op = 0;
    std::vector<ExprPtr> args;
};

struct Token {
    enum class Kind { Ident, Number, Punct, End };

    Kind kind = Kind::End;
    std::string text;
};

struct SyntaxError : std::runtime_error {
    SyntaxError(std::string kind, const std::string& message, std::string fix = {})
        : std::runtime_error(message), code(std::move(kind)), hint(std::move(fix))
    {}

    std::string code;
    std::string hint;
};

std::vector<Token> lex(std::string_view source, std::vector<ExpressionError>& errors)
{
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < source.size()) {
        const char c = source[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
        } else if (c == '#' || (c == '/' && i + 1 < source.size() && source[i + 1] == '/')) {
            errors.push_back({"comment", "Comments are not part of a value", {}});
            i = source.size();
        } else if (identStart(c)) {
            std::size_t end = i;
            while (end < source.size() && identChar(source[end])) {
                ++end;
            }
            tokens.push_back({Token::Kind::Ident, std::string(source.substr(i, end - i))});
            i = end;
        } else if (isDigit(c) || (c == '.' && i + 1 < source.size() && isDigit(source[i + 1]))) {
            std::string digits;
            while (i < source.size()) {
                const char d = source[i];
                if (isDigit(d) || d == '.') {
                    digits += d;
                } else if (d == ',' && i + 1 < source.size() && isDigit(source[i + 1])) {
                    digits += '.';
                } else {
                    break;
                }
                ++i;
            }
            tokens.push_back({Token::Kind::Number, digits});
        } else {
            const std::string_view two = source.substr(i, 2);
            if (two == "<=" || two == ">=" || two == "==") {
                tokens.push_back({Token::Kind::Punct, std::string(two)});
                i += 2;
            } else if (std::string_view("(){},.:=+-*/<>").find(c) != std::string_view::npos) {
                tokens.push_back({Token::Kind::Punct, std::string(1, c)});
                ++i;
            } else {
                errors.push_back({"character", std::string("Unexpected character '") + c + "'", {}});
                ++i;
            }
        }
    }
    tokens.push_back({Token::Kind::End, {}});
    return tokens;
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : m_tokens(std::move(tokens)) {}

    ExprPtr single()
    {
        ExprPtr result = expression();
        if (peek().kind != Token::Kind::End) {
            fail("the end of the expression");
        }
        return result;
    }

private:
    const Token& peek() const { return m_tokens[std::min(m_index, m_tokens.size() - 1)]; }

    Token next()
    {
        Token token = peek();
        if (m_index < m_tokens.size() - 1) {
            ++m_index;
        }
        return token;
    }

    bool isPunct(std::string_view text) const { return peek().kind == Token::Kind::Punct && peek().text == text; }

    static std::string describe(const Token& token)
    {
        switch (token.kind) {
        case Token::Kind::End:
            return "the end";
        case Token::Kind::Ident:
        case Token::Kind::Number:
        case Token::Kind::Punct:
            return "'" + token.text + "'";
        }
        return "?";
    }

    [[noreturn]] void fail(const std::string& expected) const
    {
        throw SyntaxError("syntax", "Expected " + expected + " but found " + describe(peek()));
    }

    void expectPunct(std::string_view text)
    {
        if (!isPunct(text)) {
            fail("'" + std::string(text) + "'");
        }
        next();
    }

    ExprPtr expression()
    {
        ExprPtr left = term();
        while (isPunct("+") || isPunct("-")) {
            const Token op = next();
            left = binary(op, left, term());
        }
        return left;
    }

    ExprPtr term()
    {
        ExprPtr left = unary();
        while (isPunct("*") || isPunct("/")) {
            const Token op = next();
            left = binary(op, left, unary());
        }
        return left;
    }

    static ExprPtr binary(const Token& op, ExprPtr left, ExprPtr right)
    {
        auto result = std::make_shared<Expr>();
        result->kind = Expr::Kind::Binary;
        result->op = op.text[0];
        result->args = {std::move(left), std::move(right)};
        return result;
    }

    ExprPtr unary()
    {
        if (isPunct("-")) {
            next();
            auto result = std::make_shared<Expr>();
            result->kind = Expr::Kind::Unary;
            result->op = '-';
            result->args = {unary()};
            return result;
        }
        return primary();
    }

    ExprPtr primary()
    {
        const Token token = peek();
        auto result = std::make_shared<Expr>();
        if (token.kind == Token::Kind::Number) {
            next();
            const std::optional<double> value = parseNumber(token.text);
            if (!value) {
                throw SyntaxError("number", "'" + token.text + "' is not a number");
            }
            if (!fixedText(*value)) {
                throw SyntaxError("number", "This number is too close to zero",
                                  "Use at most " + std::to_string(kMaxDecimals) + " decimal places");
            }
            result->kind = Expr::Kind::Number;
            result->number = *value;
            if (peek().kind == Token::Kind::Ident && isUnit(peek().text)) {
                result->unit = next().text;
            }
            return result;
        }
        if (isPunct("(")) {
            next();
            ExprPtr inner = expression();
            expectPunct(")");
            return inner;
        }
        if (token.kind != Token::Kind::Ident) {
            fail("a value");
        }
        if (token.text == kPieces) {
            throw SyntaxError("pick", "'" + token.text + "'" + std::string(kPiecesMessage), std::string(kPiecesHint));
        }
        if (isUnit(token.text)) {
            throw SyntaxError("reserved", "'" + token.text + "' is a unit and needs a number before it");
        }
        next();
        if (isPunct("(")) {
            next();
            result->kind = Expr::Kind::Call;
            result->text = token.text;
            if (!isPunct(")")) {
                result->args.push_back(expression());
                while (isPunct(",")) {
                    next();
                    result->args.push_back(expression());
                }
            }
            expectPunct(")");
            return result;
        }
        if (isPunct(".")) {
            result->kind = Expr::Kind::Path;
            result->path.push_back(token.text);
            while (isPunct(".")) {
                next();
                if (peek().kind != Token::Kind::Ident) {
                    fail("a name after '.'");
                }
                result->path.push_back(next().text);
            }
            return result;
        }
        result->kind = Expr::Kind::Name;
        result->text = token.text;
        return result;
    }

    std::vector<Token> m_tokens;
    std::size_t m_index = 0;
};

int precedence(const Expr& expr)
{
    if (expr.kind == Expr::Kind::Binary) {
        return expr.op == '+' || expr.op == '-' ? 1 : 2;
    }
    if (expr.kind == Expr::Kind::Unary) {
        return 3;
    }
    return 4;
}

std::string format(const Expr& expr);

std::string wrap(const Expr& child, bool parens)
{
    const std::string text = format(child);
    return parens ? "(" + text + ")" : text;
}

std::string format(const Expr& expr)
{
    switch (expr.kind) {
    case Expr::Kind::Number:
        return formatNumber(expr.number) + (expr.unit.empty() ? "" : " " + expr.unit);
    case Expr::Kind::Name:
        return expr.text;
    case Expr::Kind::Path: {
        std::string text;
        for (const std::string& segment : expr.path) {
            text += (text.empty() ? "" : ".") + segment;
        }
        return text;
    }
    case Expr::Kind::Call: {
        std::string text = expr.text + "(";
        for (std::size_t i = 0; i < expr.args.size(); ++i) {
            text += (i == 0 ? "" : ", ") + format(*expr.args[i]);
        }
        return text + ")";
    }
    case Expr::Kind::Unary:
        return "-" + wrap(*expr.args[0], precedence(*expr.args[0]) < 3);
    case Expr::Kind::Binary: {
        const int own = precedence(expr);
        const std::string left = wrap(*expr.args[0], precedence(*expr.args[0]) < own);
        const std::string right = wrap(*expr.args[1], precedence(*expr.args[1]) <= own);
        return left + " " + expr.op + " " + right;
    }
    }
    return {};
}

bool plainNumber(const Expr& expr)
{
    if (expr.kind == Expr::Kind::Unary) {
        return expr.args[0]->kind == Expr::Kind::Number && expr.args[0]->unit.empty();
    }
    return expr.kind == Expr::Kind::Number && expr.unit.empty();
}

bool bareSum(const Expr& expr)
{
    switch (expr.kind) {
    case Expr::Kind::Number:
        return expr.unit.empty();
    case Expr::Kind::Unary:
        return bareSum(*expr.args[0]);
    case Expr::Kind::Binary:
        return (expr.op == '+' || expr.op == '-') && bareSum(*expr.args[0]) && bareSum(*expr.args[1]);
    case Expr::Kind::Name:
    case Expr::Kind::Path:
    case Expr::Kind::Call:
        return false;
    }
    return false;
}

void giveUnit(Expr& expr, const std::string& unit)
{
    if (expr.kind == Expr::Kind::Number) {
        expr.unit = unit;
    }
    for (const ExprPtr& child : expr.args) {
        giveUnit(*child, unit);
    }
}

struct Type {
    bool bad = true;
    Dim dim;
    bool anyDim = false;
};

class Checker {
public:
    explicit Checker(const ExpressionScope& scope) : m_scope(scope) {}

    Type typeOf(Expr& expr)
    {
        switch (expr.kind) {
        case Expr::Kind::Number: {
            Type type{false};
            if (expr.unit == kPieces) {
                error("pick", "'" + format(expr) + "'" + std::string(kPiecesMessage), std::string(kPiecesHint));
                return {};
            }
            if (expr.unit.empty()) {
                type.anyDim = expr.number == 0.0;
            } else {
                type.dim = unitInfo(expr.unit).dim;
            }
            return type;
        }
        case Expr::Kind::Name:
            return name(expr);
        case Expr::Kind::Path:
            error("type", "'" + format(expr) + "' selects geometry and has no value");
            return {};
        case Expr::Kind::Call:
            if (isPlainOnlyFunction(expr.text)) {
                error("function", std::string(kPlainOnlyMessage), std::string(kPlainOnlyHint));
            } else {
                error("unknown-name", "Unknown function '" + expr.text + "'");
            }
            return {};
        case Expr::Kind::Unary:
            return typeOf(*expr.args[0]);
        case Expr::Kind::Binary:
            return binary(expr);
        }
        return {};
    }

    const std::vector<ExpressionError>& errors() const { return m_errors; }
    const std::vector<std::string>& names() const { return m_names; }

private:
    void error(const std::string& code, const std::string& message, const std::string& hint = {})
    {
        m_errors.push_back({code, message, hint});
    }

    Type name(const Expr& expr)
    {
        if (isPlainOnlyFunction(expr.text)) {
            error("function", std::string(kPlainOnlyMessage), std::string(kPlainOnlyHint));
            return {};
        }
        if (isThreadName(expr.text)) {
            error("thread", "'" + expr.text + "' is a thread, and threads can't be used here yet");
            return {};
        }
        if (isPlaneName(expr.text)) {
            error("type", "'" + expr.text + "' is a plane, not a value");
            return {};
        }
        if (std::find(m_names.begin(), m_names.end(), expr.text) == m_names.end()) {
            m_names.push_back(expr.text);
        }
        if (m_scope.values != nullptr) {
            const auto found = m_scope.values->find(expr.text);
            if (found != m_scope.values->end()) {
                return {false, found->second.dim, found->second.anyDim};
            }
        }
        error("unknown-name", "Unknown name '" + expr.text + "'", suggestion(expr.text));
        return {};
    }

    std::string suggestion(const std::string& text) const
    {
        if (std::find(m_scope.later.begin(), m_scope.later.end(), text) != m_scope.later.end()) {
            return "'" + text + "' is defined further down. Move it above this line";
        }
        std::vector<std::string> candidates;
        if (m_scope.values != nullptr) {
            for (const auto& entry : *m_scope.values) {
                candidates.push_back(entry.first);
            }
        }
        const std::string best = closest(text, candidates);
        return best.empty() ? std::string() : "Did you mean '" + best + "'?";
    }

    static std::string describe(const Type& type)
    {
        return type.anyDim ? "a number" : withArticle(describeDim(type.dim));
    }

    static bool takesFieldUnit(Dim dim) { return dim == kLengthDim || dim == kAngleDim; }

    Type binary(Expr& expr)
    {
        Type left = typeOf(*expr.args[0]);
        Type right = typeOf(*expr.args[1]);
        if (left.bad || right.bad) {
            return {};
        }
        Type result{false};
        if (expr.op == '+' || expr.op == '-') {
            if (!left.anyDim && !right.anyDim && !(left.dim == right.dim)) {
                Expr& lhs = *expr.args[0];
                Expr& rhs = *expr.args[1];
                if (left.dim == kPlainDim && bareSum(lhs) && takesFieldUnit(right.dim)) {
                    giveUnit(lhs, unitFor(right.dim));
                    left.dim = right.dim;
                } else if (right.dim == kPlainDim && bareSum(rhs) && takesFieldUnit(left.dim)) {
                    giveUnit(rhs, unitFor(left.dim));
                    right.dim = left.dim;
                } else {
                    error("units",
                          std::string("Cannot ") + (expr.op == '+' ? "add " : "subtract ") + describe(right) +
                              (expr.op == '+' ? " to " : " from ") + describe(left),
                          bareHint(lhs, left, rhs, right));
                    return {};
                }
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

    static std::string bareHint(const Expr& lhs, const Type& left, const Expr& rhs, const Type& right)
    {
        if (!(left.dim == kPlainDim) && !(right.dim == kPlainDim)) {
            return {};
        }
        const bool bareLeft = left.dim == kPlainDim;
        const Expr& bare = bareLeft ? lhs : rhs;
        const Dim other = bareLeft ? right.dim : left.dim;
        const std::string unit = unitFor(other);
        if (unit.empty()) {
            return "A bare number has no unit. Add one, like 2 mm";
        }
        return "Add a unit, like " + (plainNumber(bare) ? format(bare) : std::string("2")) + " " + unit;
    }

    const ExpressionScope& m_scope;
    std::vector<ExpressionError> m_errors;
    std::vector<std::string> m_names;
};

struct EvalError : std::runtime_error {
    explicit EvalError(const std::string& message) : std::runtime_error(message) {}
};

ParameterValue evaluate(const Expr& expr, const ParameterValues& params)
{
    switch (expr.kind) {
    case Expr::Kind::Number: {
        const UnitInfo& info = unitInfo(expr.unit);
        return {expr.number * info.scale, info.dim};
    }
    case Expr::Kind::Name: {
        const auto found = params.find(expr.text);
        if (found == params.end()) {
            throw EvalError("'" + expr.text + "' has no value here");
        }
        return found->second;
    }
    case Expr::Kind::Path:
    case Expr::Kind::Call:
        throw EvalError("'" + format(expr) + "' has no value");
    case Expr::Kind::Unary: {
        ParameterValue inner = evaluate(*expr.args[0], params);
        inner.value = -inner.value;
        return inner;
    }
    case Expr::Kind::Binary: {
        const ParameterValue a = evaluate(*expr.args[0], params);
        const ParameterValue b = evaluate(*expr.args[1], params);
        switch (expr.op) {
        case '+':
            return {a.value + b.value, a.dim == Dim{} ? b.dim : a.dim};
        case '-':
            return {a.value - b.value, a.dim == Dim{} ? b.dim : a.dim};
        case '*':
            return {a.value * b.value, {a.dim.length + b.dim.length, a.dim.angle + b.dim.angle}};
        case '/':
            if (b.value == 0.0) {
                throw EvalError("Division by zero");
            }
            return {a.value / b.value, {a.dim.length - b.dim.length, a.dim.angle - b.dim.angle}};
        default:
            break;
        }
        break;
    }
    }
    throw EvalError("Cannot evaluate this expression");
}

bool usesPieces(std::string_view text)
{
    std::vector<ExpressionError> errors;
    const std::vector<Token> tokens = lex(text, errors);
    return std::ranges::any_of(
        tokens, [](const Token& token) { return token.kind == Token::Kind::Ident && token.text == kPieces; });
}

struct Checked {
    ExprPtr expr;
    Type type;
    ExpressionResult result;
};

Checked parseAndCheck(std::string_view text, const ExpressionScope& scope)
{
    Checked checked;
    if (text.find('^') != std::string_view::npos) {
        checked.result.error = ExpressionError{"function", std::string(kPlainOnlyMessage), std::string(kPlainOnlyHint)};
        return checked;
    }
    std::vector<ExpressionError> errors;
    std::vector<Token> tokens = lex(text, errors);
    if (!errors.empty()) {
        checked.result.error = errors.front();
        return checked;
    }
    try {
        checked.expr = Parser(std::move(tokens)).single();
    } catch (const SyntaxError& error) {
        checked.result.error = ExpressionError{error.code, error.what(), error.hint};
        return checked;
    }
    Checker checker(scope);
    checked.type = checker.typeOf(*checked.expr);
    checked.result.names = checker.names();
    checked.result.canonical = format(*checked.expr);
    if (!checker.errors().empty()) {
        checked.result.error = checker.errors().front();
        return checked;
    }
    static const ParameterValues kNoValues;
    try {
        const ParameterValue value = evaluate(*checked.expr, scope.values != nullptr ? *scope.values : kNoValues);
        if (!std::isfinite(value.value)) {
            checked.result.error = ExpressionError{"build", "The value is too large", {}};
            return checked;
        }
        checked.result.value = value.value;
        checked.result.dim = checked.type.anyDim ? kPlainDim : value.dim;
        checked.result.anyDim = checked.type.anyDim;
    } catch (const EvalError& error) {
        checked.result.error = ExpressionError{"build", error.what(), {}};
    }
    return checked;
}

Dim dimOf(Quantity quantity)
{
    switch (quantity) {
    case Quantity::Length:
        return kLengthDim;
    case Quantity::Angle:
        return kAngleDim;
    case Quantity::Count:
        return kPlainDim;
    }
    return kPlainDim;
}

} // namespace

std::optional<ExpressionError> checkParameterName(std::string_view name)
{
    if (name.empty() || !identStart(name.front()) || !std::all_of(name.begin(), name.end(), identChar)) {
        return ExpressionError{"name", "A name starts with a letter or _ and has only letters, digits and _", {}};
    }
    const std::string text(name);
    if (isUnit(name)) {
        return ExpressionError{"reserved", "'" + text + "' is a unit and cannot name a parameter", "Pick another name"};
    }
    if (isThreadName(name) || isPlaneName(name)) {
        return ExpressionError{"reserved", "'" + text + "' reads as a thread or plane and cannot be a name",
                               "Pick another name"};
    }
    if (isPlainOnlyWord(name)) {
        return ExpressionError{"reserved", "'" + text + "' is a function or unit of plain values", "Pick another name"};
    }
    if (lists(kKeywords, name)) {
        return ExpressionError{"reserved", "'" + text + "' is a keyword and cannot name a parameter",
                               "Pick another name"};
    }
    if (lists(kFeatureKinds, name)) {
        return ExpressionError{"reserved", "'" + text + "' is a kind of feature and cannot name a parameter",
                               "Pick another name"};
    }
    return std::nullopt;
}

bool namesParameter(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (isDigit(c) || c == '.' || c == ',') {
            ++i;
            while (i < text.size()) {
                const char d = text[i];
                if (isDigit(d) || d == '.' || d == ',') {
                    ++i;
                } else if ((d == 'e' || d == 'E') && i + 1 < text.size() &&
                           (isDigit(text[i + 1]) || text[i + 1] == '-')) {
                    i += 2;
                } else {
                    break;
                }
            }
        } else if (identStart(c)) {
            std::size_t end = i;
            while (end < text.size() && identChar(text[end])) {
                ++end;
            }
            const std::string_view word = text.substr(i, end - i);
            if (!isUnit(word) && !isPlainOnlyWord(word)) {
                return true;
            }
            i = end;
        } else {
            ++i;
        }
    }
    return false;
}

ExpressionResult evaluateParameter(std::string_view text, const ExpressionScope& scope)
{
    return parseAndCheck(text, scope).result;
}

ExpressionResult evaluateField(std::string_view text, Quantity quantity, const ExpressionScope& scope)
{
    const Dim want = dimOf(quantity);
    if (!namesParameter(text) && !usesPieces(text)) {
        ExpressionResult result;
        const std::optional<double> value = evaluate(text, quantity);
        if (!value) {
            result.error = ExpressionError{"value", std::string(kUnreadableMessage), std::string(kUnreadableHint)};
            return result;
        }
        result.value = *value;
        result.dim = want;
        return result;
    }
    Checked checked = parseAndCheck(text, scope);
    ExpressionResult& result = checked.result;
    if (!result.ok()) {
        return result;
    }
    if (!checked.type.anyDim && !(result.dim == want)) {
        std::string hint;
        if (result.dim == kPlainDim && want != kPlainDim) {
            if (checked.expr->kind == Expr::Kind::Name) {
                hint = "Parameter " + checked.expr->text + " has no unit. Add one where it is defined, like 10 " +
                       unitFor(want);
            } else {
                hint = "Write " + result.canonical + " " + unitFor(want);
            }
        }
        result.error = ExpressionError{
            "units", "Needs " + withArticle(describeDim(want)) + ", not " + withArticle(describeDim(result.dim)), hint};
        return result;
    }
    result.dim = want;
    return result;
}

std::string renameInExpression(std::string_view canonical, std::string_view from, std::string_view to)
{
    std::vector<ExpressionError> errors;
    std::vector<Token> tokens = lex(canonical, errors);
    if (!errors.empty()) {
        return std::string(canonical);
    }
    ExprPtr expr;
    try {
        expr = Parser(std::move(tokens)).single();
    } catch (const SyntaxError&) {
        return std::string(canonical);
    }
    std::vector<Expr*> pending{expr.get()};
    while (!pending.empty()) {
        Expr* node = pending.back();
        pending.pop_back();
        if (node->kind == Expr::Kind::Name && node->text == from) {
            node->text = std::string(to);
        }
        for (const ExprPtr& child : node->args) {
            pending.push_back(child.get());
        }
    }
    return format(*expr);
}

std::string formatNumber(double value)
{
    if (std::optional<std::string> text = fixedText(value)) {
        return *text;
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(kMaxDecimals) << value;
    const std::optional<double> back = parseNumber(stream.str());
    return back && *back != 0.0 ? stream.str() : "0";
}

std::string formatParameterValue(const ParameterValue& value)
{
    const double rounded = std::round(value.value * 1000.0) / 1000.0;
    const std::string text = formatNumber(rounded == 0.0 ? 0.0 : rounded);
    const std::string unit = unitFor(value.dim);
    return unit.empty() ? text : text + " " + unit;
}

std::string describeDim(Dim dim)
{
    if (dim == kPlainDim) {
        return "plain number";
    }
    if (dim == kLengthDim) {
        return "length";
    }
    if (dim == Dim{2, 0}) {
        return "area";
    }
    if (dim == Dim{3, 0}) {
        return "volume";
    }
    if (dim == kAngleDim) {
        return "angle";
    }
    return "length^" + std::to_string(dim.length) + " angle^" + std::to_string(dim.angle);
}

} // namespace kalve137::core
