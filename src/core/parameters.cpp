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
