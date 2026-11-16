// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lang/parse.h"

#include "lang/format.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <regex>
#include <stdexcept>
#include <utility>

namespace kalve137::lang {

namespace {

struct Token {
    enum class Kind { Ident, Number, Punct, Newline, End };

    Kind kind = Kind::End;
    std::string text;
    Span span;
};

struct SyntaxError : std::runtime_error {
    SyntaxError(Span at, std::string code, const std::string& message, std::string fix = {})
        : std::runtime_error(message), span(at), code(std::move(code)), hint(std::move(fix))
    {}

    Span span;
    std::string code;
    std::string hint;
};

constexpr std::array kUnits{"mm", "cm", "m", "in", "deg", "rad", "mm2", "cm2", "mm3", "cm3", "pieces"};
constexpr std::array kKeywords{"part",  "param",  "sketch", "on",   "rect", "circle", "center",   "size",
                               "point", "line",   "arc",    "near", "from", "to",     "diameter", "extrude",
                               "cut",   "fillet", "check",  "up",   "down", "depth",  "through",  "radius"};

bool identStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool identChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::vector<Token> lex(const std::string& source, std::vector<Diagnostic>& diagnostics)
{
    std::vector<Token> tokens;
    int line = 1;
    int column = 1;
    std::size_t i = 0;
    auto advance = [&](std::size_t count) {
        for (std::size_t k = 0; k < count && i < source.size(); ++k, ++i) {
            if (source[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
    };
    while (i < source.size()) {
        const char c = source[i];
        const Span span{line, column};
        if (c == '\n') {
            tokens.push_back({Token::Kind::Newline, "\n", span});
            advance(1);
        } else if (c == ' ' || c == '\t' || c == '\r') {
            advance(1);
        } else if (c == '#' || (c == '/' && i + 1 < source.size() && source[i + 1] == '/')) {
            diagnostics.push_back({Diagnostic::Severity::Error,
                                   "comment",
                                   span,
                                   {},
                                   "Comments are not part of a design",
                                   "Say it as a name or a check, which the machine can hold you to"});
            while (i < source.size() && source[i] != '\n') {
                advance(1);
            }
        } else if (identStart(c)) {
            std::size_t end = i;
            while (end < source.size() && identChar(source[end])) {
                ++end;
            }
            std::string word = source.substr(i, end - i);
            if (std::regex_match(word, std::regex("M[0-9]+")) && end + 1 < source.size() && source[end] == '.' &&
                std::isdigit(static_cast<unsigned char>(source[end + 1])) != 0) {
                ++end;
                while (end < source.size() && std::isdigit(static_cast<unsigned char>(source[end])) != 0) {
                    ++end;
                }
                word = source.substr(i, end - i);
            }
            tokens.push_back({Token::Kind::Ident, word, span});
            advance(end - i);
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
                   (c == '.' && i + 1 < source.size() &&
                    std::isdigit(static_cast<unsigned char>(source[i + 1])) != 0)) {
            std::size_t end = i;
            while (end < source.size() &&
                   (std::isdigit(static_cast<unsigned char>(source[end])) != 0 || source[end] == '.')) {
                ++end;
            }
            tokens.push_back({Token::Kind::Number, source.substr(i, end - i), span});
            advance(end - i);
        } else {
            const std::string two = source.substr(i, 2);
            if (two == "<=" || two == ">=" || two == "==") {
                tokens.push_back({Token::Kind::Punct, two, span});
                advance(2);
            } else if (std::string("(){},.:=+-*/<>").find(c) != std::string::npos) {
                tokens.push_back({Token::Kind::Punct, std::string(1, c), span});
                advance(1);
            } else {
                diagnostics.push_back({Diagnostic::Severity::Error,
                                       "character",
                                       span,
                                       {},
                                       std::string("Unexpected character '") + c + "'",
                                       {}});
                advance(1);
            }
        }
    }
    tokens.push_back({Token::Kind::End, {}, {line, column}});
    return tokens;
}

class Parser {
public:
    Parser(std::vector<Token> tokens, std::vector<Diagnostic>& diagnostics)
        : m_tokens(std::move(tokens)), m_diagnostics(diagnostics)
    {}

    ExprPtr single()
    {
        try {
            ExprPtr result = expression();
            if (peek().kind != Token::Kind::End && peek().kind != Token::Kind::Newline) {
                fail("the end of the expression");
            }
            return result;
        } catch (const SyntaxError& error) {
            m_diagnostics.push_back(
                {Diagnostic::Severity::Error, error.code, error.span, {}, error.what(), error.hint});
            return nullptr;
        }
    }

    Document run()
    {
        Document document;
        while (true) {
            skipNewlines();
            if (peek().kind == Token::Kind::End) {
                break;
            }
            const std::size_t start = m_index;
            try {
                document.statements.push_back(statement());
            } catch (const SyntaxError& error) {
                m_diagnostics.push_back(
                    {Diagnostic::Severity::Error, error.code, error.span, {}, error.what(), error.hint});
                recover(start);
            }
        }
        return document;
    }

private:
    const Token& peek(std::size_t ahead = 0) const { return m_tokens[std::min(m_index + ahead, m_tokens.size() - 1)]; }

    Token next()
    {
        Token token = peek();
        if (m_index < m_tokens.size() - 1) {
            ++m_index;
        }
        return token;
    }

    bool isPunct(const std::string& text) const { return peek().kind == Token::Kind::Punct && peek().text == text; }

    bool isWord(const std::string& text) const { return peek().kind == Token::Kind::Ident && peek().text == text; }

    static std::string describe(const Token& token)
    {
        switch (token.kind) {
        case Token::Kind::Newline:
            return "the end of the line";
        case Token::Kind::End:
            return "the end of the file";
        case Token::Kind::Ident:
        case Token::Kind::Number:
        case Token::Kind::Punct:
            return "'" + token.text + "'";
        }
        return "?";
    }

    [[noreturn]] void fail(const std::string& expected) const
    {
        throw SyntaxError(peek().span, "syntax", "Expected " + expected + " but found " + describe(peek()));
    }

    void expectPunct(const std::string& text)
    {
        if (!isPunct(text)) {
            fail("'" + text + "'");
        }
        next();
    }

    void expectWord(const std::string& text)
    {
        if (!isWord(text)) {
            fail("'" + text + "'");
        }
        next();
    }

    Token name(const std::string& what)
    {
        if (peek().kind != Token::Kind::Ident) {
            fail(what);
        }
        if (isUnit(peek().text)) {
            throw SyntaxError(peek().span, "reserved", "'" + peek().text + "' is a unit and cannot name " + what,
                              "Pick another name");
        }
        return next();
    }

    void endOfLine()
    {
        if (peek().kind != Token::Kind::Newline && peek().kind != Token::Kind::End) {
            fail("the end of the line");
        }
    }

    void skipNewlines()
    {
        while (peek().kind == Token::Kind::Newline) {
            next();
        }
    }

    void recover(std::size_t start)
    {
        int depth = 0;
        m_index = std::max(m_index, start);
        while (peek().kind != Token::Kind::End) {
            if (isPunct("{")) {
                ++depth;
            } else if (isPunct("}")) {
                --depth;
                if (depth <= 0) {
                    next();
                    return;
                }
            } else if (peek().kind == Token::Kind::Newline && depth <= 0) {
                return;
            }
            next();
        }
    }

    Statement statement()
    {
        const Token head = peek();
        if (head.kind != Token::Kind::Ident) {
            fail("a statement");
        }
        Statement result;
        result.span = head.span;
        next();
        if (head.text == "part") {
            result.kind = Statement::Kind::Part;
            result.name = name("the part").text;
        } else if (head.text == "param") {
            result.kind = Statement::Kind::Param;
            result.name = name("the parameter").text;
            expectPunct("=");
            result.value = expression();
        } else if (head.text == "sketch") {
            result.kind = Statement::Kind::Sketch;
            result.name = name("the sketch").text;
            expectWord("on");
            result.plane = expression();
            expectPunct("{");
            while (true) {
                skipNewlines();
                if (isPunct("}")) {
                    next();
                    break;
                }
                result.items.push_back(item());
                endOfLine();
            }
        } else if (head.text == "extrude") {
            result.kind = Statement::Kind::Extrude;
            result.name = name("the extrude").text;
            expectPunct("=");
            const Token sketch = name("the sketch to extrude");
            result.sketch = sketch.text;
            result.sketchSpan = sketch.span;
            if (isWord("down")) {
                result.down = true;
            } else if (!isWord("up")) {
                fail("'up' or 'down'");
            }
            next();
            result.value = expression();
        } else if (head.text == "cut") {
            result.kind = Statement::Kind::Cut;
            result.name = name("the cut").text;
            expectPunct("=");
            const Token sketch = name("the sketch to cut");
            result.sketch = sketch.text;
            result.sketchSpan = sketch.span;
            if (isWord("through")) {
                next();
                result.through = true;
            } else {
                expectWord("depth");
                result.value = expression();
            }
        } else if (head.text == "fillet") {
            result.kind = Statement::Kind::Fillet;
            result.name = name("the fillet").text;
            expectPunct("=");
            result.lhs = expression();
            expectWord("radius");
            result.value = expression();
        } else if (head.text == "check") {
            result.kind = Statement::Kind::Check;
            result.name = name("the check").text;
            expectPunct(":");
            result.lhs = expression();
            for (const char* comparison : {"<=", ">=", "==", "<", ">"}) {
                if (isPunct(comparison)) {
                    result.comparison = comparison;
                }
            }
            if (result.comparison.empty()) {
                fail("a comparison like >= or <=");
            }
            next();
            result.rhs = expression();
        } else {
            throw SyntaxError(head.span, "statement", "Unknown statement '" + head.text + "'",
                              "Statements are part, param, sketch, extrude, cut, fillet and check");
        }
        endOfLine();
        return result;
    }

    Point point()
    {
        expectPunct("(");
        Point result;
        result.x = expression();
        expectPunct(",");
        result.y = expression();
        expectPunct(")");
        return result;
    }

    SketchItem item()
    {
        const Token head = peek();
        SketchItem result;
        result.span = head.span;
        if (head.kind == Token::Kind::Ident && head.text == "rect") {
            next();
            result.kind = SketchItem::Kind::Rect;
            result.name = name("the rectangle").text;
            expectWord("center");
            result.center = point();
            expectWord("size");
            result.size = point();
        } else if (head.kind == Token::Kind::Ident && head.text == "circle") {
            next();
            result.kind = SketchItem::Kind::Circle;
            result.name = name("the circle").text;
            expectWord("center");
            if (isPunct("(")) {
                result.center = point();
            } else {
                ref(result, "the center point");
            }
            expectWord("diameter");
            result.diameter = expression();
        } else if (head.kind == Token::Kind::Ident && head.text == "point") {
            next();
            result.kind = SketchItem::Kind::Point;
            result.name = name("the point").text;
            if (peek().kind == Token::Kind::Ident && peek().text == "at") {
                const Token at = next();
                const Point written = point();
                const std::string where = "(" + format(*written.x) + ", " + format(*written.y) + ")";
                throw SyntaxError(at.span, "guess", "A written point position is a guess, so it reads 'near', not 'at'",
                                  "Write point " + result.name + " near " + where + ", and add at(" + result.name +
                                      ") = " + where + " if it must stay there");
            }
            if (peek().kind == Token::Kind::Ident && peek().text == "near") {
                next();
                result.center = point();
            }
        } else if (head.kind == Token::Kind::Ident && (head.text == "line" || head.text == "arc")) {
            next();
            const bool line = head.text == "line";
            result.kind = line ? SketchItem::Kind::Line : SketchItem::Kind::Arc;
            result.name = name(line ? "the line" : "the arc").text;
            expectWord("from");
            ref(result, "the start point");
            expectWord("to");
            ref(result, "the end point");
            if (!line) {
                expectWord("center");
                ref(result, "the center point");
            }
        } else if (head.kind == Token::Kind::Ident && peek(1).kind == Token::Kind::Punct && peek(1).text == "(") {
            result.kind = SketchItem::Kind::Constraint;
            result.call = primary();
            if (isPunct("=")) {
                next();
                if (head.text == "at") {
                    result.center = point();
                } else {
                    result.value = expression();
                }
            }
        } else {
            throw SyntaxError(head.span, "item", "Expected a sketch item but found " + describe(head),
                              "Sketch items are rect, circle, point, line, arc and constraints like horizontal(a)");
        }
        return result;
    }

    void ref(SketchItem& item, const std::string& what)
    {
        const Token token = name(what);
        item.refs.push_back(token.text);
        item.refSpans.push_back(token.span);
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
        result->span = left->span;
        result->op = op.text[0];
        result->args = {std::move(left), std::move(right)};
        return result;
    }

    ExprPtr unary()
    {
        if (isPunct("-")) {
            const Token op = next();
            auto result = std::make_shared<Expr>();
            result->kind = Expr::Kind::Unary;
            result->span = op.span;
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
        result->span = token.span;
        if (token.kind == Token::Kind::Number) {
            next();
            double value = 0.0;
            const auto [end, error] = std::from_chars(token.text.data(), token.text.data() + token.text.size(), value);
            if (error != std::errc() || end != token.text.data() + token.text.size()) {
                throw SyntaxError(token.span, "number", "'" + token.text + "' is not a number");
            }
            result->kind = Expr::Kind::Number;
            result->number = value;
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
        if (isUnit(token.text)) {
            throw SyntaxError(token.span, "reserved", "'" + token.text + "' is a unit and needs a number before it");
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
        result->kind = isThreadName(token.text) ? Expr::Kind::Thread : Expr::Kind::Name;
        result->text = token.text;
        return result;
    }

    std::vector<Token> m_tokens;
    std::size_t m_index = 0;
    std::vector<Diagnostic>& m_diagnostics;
};

} // namespace

bool isUnit(const std::string& word)
{
    return std::find(kUnits.begin(), kUnits.end(), word) != kUnits.end();
}

bool isKeyword(const std::string& word)
{
    return std::find(kKeywords.begin(), kKeywords.end(), word) != kKeywords.end();
}

std::vector<std::string> keywords()
{
    return {kKeywords.begin(), kKeywords.end()};
}

std::vector<std::string> units()
{
    return {kUnits.begin(), kUnits.end()};
}

bool isThreadName(const std::string& word)
{
    static const std::regex kThreadName("M[0-9]+(\\.[0-9]+)?");
    return std::regex_match(word, kThreadName);
}

ParseResult parse(const std::string& source)
{
    ParseResult result;
    std::vector<Token> tokens = lex(source, result.diagnostics);
    Parser parser(std::move(tokens), result.diagnostics);
    result.document = parser.run();
    return result;
}

ExpressionResult parseExpression(const std::string& text)
{
    ExpressionResult result;
    std::vector<Token> tokens = lex(text, result.diagnostics);
    Parser parser(std::move(tokens), result.diagnostics);
    result.expr = parser.single();
    return result;
}

const Statement* Document::find(const std::string& name) const
{
    for (const Statement& statement : statements) {
        if (statement.name == name) {
            return &statement;
        }
    }
    return nullptr;
}

bool hasErrors(const std::vector<Diagnostic>& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const Diagnostic& d) { return d.severity == Diagnostic::Severity::Error; });
}

std::string toString(Diagnostic::Severity severity)
{
    switch (severity) {
    case Diagnostic::Severity::Error:
        return "error";
    case Diagnostic::Severity::Warning:
        return "warning";
    case Diagnostic::Severity::Note:
        return "note";
    }
    return "?";
}

std::string toString(Statement::Kind kind)
{
    switch (kind) {
    case Statement::Kind::Part:
        return "part";
    case Statement::Kind::Param:
        return "param";
    case Statement::Kind::Sketch:
        return "sketch";
    case Statement::Kind::Extrude:
        return "extrude";
    case Statement::Kind::Cut:
        return "cut";
    case Statement::Kind::Fillet:
        return "fillet";
    case Statement::Kind::Check:
        return "check";
    }
    return "?";
}

std::string toText(const Diagnostic& diagnostic, const std::string& file)
{
    std::string text = file.empty() ? std::string() : file + ":";
    text += std::to_string(diagnostic.span.line) + ":" + std::to_string(diagnostic.span.column) + ": " +
            toString(diagnostic.severity) + " [" + diagnostic.code + "]";
    if (!diagnostic.statement.empty()) {
        text += " " + diagnostic.statement + ":";
    }
    text += " " + diagnostic.message;
    if (!diagnostic.hint.empty()) {
        text += "\n    hint: " + diagnostic.hint;
    }
    return text;
}

} // namespace kalve137::lang
