// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rebuild/suggest.h"

#include "lang/edit.h"
#include "lang/format.h"
#include "lang/parse.h"
#include "rebuild/rebuild.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

namespace kalve137::rebuild {

namespace {

constexpr double kStep = 0.1;
constexpr int kNewtonSteps = 4;

const model::CheckResult* findCheck(const model::Snapshot& snapshot, const std::string& name)
{
    for (const model::CheckResult& check : snapshot.checks) {
        if (check.name == name) {
            return &check;
        }
    }
    return nullptr;
}

bool clean(const model::Snapshot& snapshot)
{
    return snapshot.complete &&
           std::all_of(snapshot.diagnostics.begin(), snapshot.diagnostics.end(), [](const auto& d) {
               return d.severity != lang::Diagnostic::Severity::Error || d.code == "check";
           });
}

double rounded(double value, double delta, const std::string& comparison)
{
    if (comparison == "==") {
        return std::round(value * 1000.0) / 1000.0;
    }
    return delta > 0 ? std::ceil(value / kStep - 1e-9) * kStep : std::floor(value / kStep + 1e-9) * kStep;
}

bool literal(const lang::Expr& expr)
{
    return expr.kind == lang::Expr::Kind::Number || (expr.kind == lang::Expr::Kind::Unary && expr.args.size() == 1 &&
                                                     expr.args[0]->kind == lang::Expr::Kind::Number);
}

std::set<std::string> literalParams(const std::string& source)
{
    std::set<std::string> names;
    for (const lang::Statement& s : lang::parse(source).document.statements) {
        if (s.kind == lang::Statement::Kind::Param && s.value && literal(*s.value)) {
            names.insert(s.name);
        }
    }
    return names;
}

} // namespace

std::vector<Suggestion> suggest(const std::string& source)
{
    std::vector<Suggestion> suggestions;
    const model::Snapshot base = build(source, {false, {}});
    if (!clean(base)) {
        return suggestions;
    }
    const std::set<std::string> literals = literalParams(source);
    for (const model::CheckResult& failed : base.checks) {
        if (failed.passed) {
            continue;
        }
        for (const auto& [param, quantity] : base.params) {
            if (quantity.thread || !(quantity.dim == lang::Dim{1, 0}) || literals.count(param) == 0) {
                continue;
            }
            Suggestion s;
            s.check = failed.name;
            s.param = param;
            s.from = quantity.value;
            double value = quantity.value;
            double lhs = failed.lhs.value;
            double rhs = failed.rhs.value;
            std::optional<double> answer;
            for (int step = 0; step < kNewtonSteps; ++step) {
                const double h = std::max(0.25, std::abs(value) * 0.05);
                const model::Snapshot probe = build(source, {false, {{param, value + h}}});
                ++s.builds;
                const model::CheckResult* moved = findCheck(probe, failed.name);
                if (moved == nullptr || !clean(probe)) {
                    break;
                }
                const double slope = ((moved->lhs.value - moved->rhs.value) - (lhs - rhs)) / h;
                if (std::abs(slope) < 1e-9) {
                    break;
                }
                const double delta = -(lhs - rhs) / slope;
                const double candidate = rounded(value + delta, delta, failed.comparison);
                if (quantity.value > 0.0 && candidate <= 0.0) {
                    break;
                }
                const model::Snapshot trial = build(source, {false, {{param, candidate}}});
                ++s.builds;
                const model::CheckResult* result = findCheck(trial, failed.name);
                if (result == nullptr || !clean(trial)) {
                    break;
                }
                if (result->passed) {
                    answer = candidate;
                    s.allChecksPass = std::all_of(trial.checks.begin(), trial.checks.end(),
                                                  [](const model::CheckResult& c) { return c.passed; });
                    break;
                }
                value = candidate;
                lhs = result->lhs.value;
                rhs = result->rhs.value;
            }
            if (!answer) {
                continue;
            }
            s.to = *answer;
            s.fixesCheck = true;
            s.value = lang::formatNumber(std::round(*answer * 1000.0) / 1000.0) + " mm";
            s.source = lang::setParam(source, param, s.value);
            suggestions.push_back(std::move(s));
        }
    }
    std::stable_sort(suggestions.begin(), suggestions.end(), [](const Suggestion& a, const Suggestion& b) {
        if (a.allChecksPass != b.allChecksPass) {
            return a.allChecksPass;
        }
        const double ra = std::abs(a.to - a.from) / std::max(1.0, std::abs(a.from));
        const double rb = std::abs(b.to - b.from) / std::max(1.0, std::abs(b.from));
        return ra < rb;
    });
    return suggestions;
}

} // namespace kalve137::rebuild
