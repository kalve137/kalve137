// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/expression.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <numbers>
#include <string>

namespace kalve137::core {

namespace {

constexpr double kDegree = std::numbers::pi / 180.0;
constexpr std::string_view kDegreeSign = "\xC2\xB0";

struct Unit {
    std::string_view name;
    Quantity quantity;
    double factor;
};

constexpr Unit kUnits[] = {
    {"mm", Quantity::Length, 1.0},       {"cm", Quantity::Length, 10.0},
    {"m", Quantity::Length, 1000.0},     {"um", Quantity::Length, 0.001},
    {"in", Quantity::Length, 25.4},      {"\"", Quantity::Length, 25.4},
    {"ft", Quantity::Length, 304.8},     {"deg", Quantity::Angle, 1.0},
    {kDegreeSign, Quantity::Angle, 1.0}, {"rad", Quantity::Angle, 180.0 / std::numbers::pi},
};

class Parser {
public:
    Parser(std::string_view text, Quantity quantity) : m_text(text), m_quantity(quantity) {}

    std::optional<double> run()
    {
        const std::optional<double> value = expression();
        skipSpace();
        if (!value || m_pos != m_text.size() || !std::isfinite(*value)) {
            return std::nullopt;
        }
        return value;
    }

private:
    void skipSpace()
    {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) {
            ++m_pos;
        }
    }

    bool take(char c)
    {
        skipSpace();
        if (m_pos < m_text.size() && m_text[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    std::optional<double> expression()
    {
        std::optional<double> value = term();
        while (value) {
            if (take('+')) {
                const std::optional<double> rhs = term();
                value = rhs ? std::optional(*value + *rhs) : std::nullopt;
            } else if (take('-')) {
                const std::optional<double> rhs = term();
                value = rhs ? std::optional(*value - *rhs) : std::nullopt;
            } else {
                break;
            }
        }
        return value;
    }

    std::optional<double> term()
    {
        std::optional<double> value = power();
        while (value) {
            if (take('*')) {
                const std::optional<double> rhs = power();
                value = rhs ? std::optional(*value * *rhs) : std::nullopt;
            } else if (take('/')) {
                const std::optional<double> rhs = power();
                value = rhs && *rhs != 0.0 ? std::optional(*value / *rhs) : std::nullopt;
            } else {
                break;
            }
        }
        return value;
    }

    std::optional<double> power()
    {
        const std::optional<double> base = unary();
        if (base && take('^')) {
            const std::optional<double> exponent = power();
            return exponent ? std::optional(std::pow(*base, *exponent)) : std::nullopt;
        }
        return base;
    }

    std::optional<double> unary()
    {
        if (take('-')) {
            const std::optional<double> value = unary();
            return value ? std::optional(-*value) : std::nullopt;
        }
        if (take('+')) {
            return unary();
        }
        const std::optional<double> value = primary();
        return value ? withUnit(*value) : std::nullopt;
    }

    std::optional<double> primary()
    {
        skipSpace();
        if (take('(')) {
            const std::optional<double> value = expression();
            return value && take(')') ? value : std::nullopt;
        }
        if (m_pos < m_text.size() &&
            (std::isdigit(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '.' || m_text[m_pos] == ',')) {
            return number();
        }
        const std::string name = identifier();
        if (name == "pi") {
            return std::numbers::pi;
        }
        double (*function)(double) = nullptr;
        bool takesAngle = false;
        if (name == "sqrt") {
            function = [](double x) { return std::sqrt(x); };
        } else if (name == "sin") {
            function = [](double x) { return std::sin(x * kDegree); };
            takesAngle = true;
        } else if (name == "cos") {
            function = [](double x) { return std::cos(x * kDegree); };
            takesAngle = true;
        } else if (name == "tan") {
            function = [](double x) { return std::tan(x * kDegree); };
            takesAngle = true;
        }
        if (function == nullptr || !take('(')) {
            return std::nullopt;
        }
        const Quantity outer = m_quantity;
        if (takesAngle) {
            m_quantity = Quantity::Angle;
        }
        const std::optional<double> argument = expression();
        m_quantity = outer;
        if (!argument || !take(')')) {
            return std::nullopt;
        }
        return function(*argument);
    }

    std::optional<double> number()
    {
        std::string digits;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (std::isdigit(static_cast<unsigned char>(c))) {
                digits += c;
            } else if (c == '.' || c == ',') {
                digits += '.';
            } else if ((c == 'e' || c == 'E') && m_pos + 1 < m_text.size() &&
                       (std::isdigit(static_cast<unsigned char>(m_text[m_pos + 1])) || m_text[m_pos + 1] == '-')) {
                digits += c;
                if (m_text[m_pos + 1] == '-') {
                    digits += '-';
                    ++m_pos;
                }
            } else {
                break;
            }
            ++m_pos;
        }
        double value = 0.0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (error != std::errc() || end != digits.data() + digits.size()) {
            return std::nullopt;
        }
        return value;
    }

    std::string identifier()
    {
        skipSpace();
        std::string name;
        while (m_pos < m_text.size() && std::isalpha(static_cast<unsigned char>(m_text[m_pos]))) {
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(m_text[m_pos])));
            ++m_pos;
        }
        return name;
    }

    std::optional<double> withUnit(double value)
    {
        skipSpace();
        const std::string_view rest = m_text.substr(m_pos);
        const Unit* best = nullptr;
        for (const Unit& unit : kUnits) {
            if (!rest.starts_with(unit.name)) {
                continue;
            }
            const std::size_t after = unit.name.size();
            const bool alphabetic = std::isalpha(static_cast<unsigned char>(unit.name.front()));
            if (alphabetic && after < rest.size() && std::isalpha(static_cast<unsigned char>(rest[after]))) {
                continue;
            }
            if (best == nullptr || unit.name.size() > best->name.size()) {
                best = &unit;
            }
        }
        if (best == nullptr) {
            return value;
        }
        if (best->quantity != m_quantity) {
            return std::nullopt;
        }
        m_pos += best->name.size();
        return value * best->factor;
    }

    std::string_view m_text;
    Quantity m_quantity;
    std::size_t m_pos = 0;
};

} // namespace

std::optional<double> evaluate(std::string_view text, Quantity quantity)
{
    return Parser(text, quantity).run();
}

} // namespace kalve137::core
