// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lang/check.h"
#include "lang/edit.h"
#include "lang/format.h"
#include "lang/parse.h"
#include "lang/reference.h"
#include "look/image.h"
#include "look/raster.h"
#include "model/json.h"
#include "model/select.h"
#include "rebuild/rebuild.h"
#include "rebuild/suggest.h"

#include <QGuiApplication>
#include <QImage>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace kalve137;

constexpr const char* kUsage = R"(kalvec, the Kalve design compiler

  kalvec help                           the language on one page
  kalvec fmt FILE [--write | --check]   print, rewrite or check the canonical form
  kalvec check FILE [--json]            parse and check without the kernel
  kalvec build FILE [--json] [--mesh]   rebuild and run the checks
  kalvec query FILE SELECTOR [--json] [--at STATEMENT]
                                        resolve a selector, '*' lists every face
  kalvec describe FILE [--at STATEMENT] the part in words
  kalvec render FILE OUT.png [--view iso|iso-back|iso-below|top|front|right]
                [--size WxH] [--labels] [--highlight SELECTOR] [--at STATEMENT]
  kalvec export FILE OUT.step           write STEP
  kalvec set FILE PARAM VALUE           change a param and rewrite the file canonically
  kalvec suggest FILE [--json] [--apply] param changes that make failed checks pass
  kalvec settle FILE [--write | --check] write solved positions into the point guesses

--at STATEMENT shows the body as that statement saw it, before it ran.
Exit status is 0 when everything passes, 1 on diagnostics or failed checks and 2 on bad usage.
)";

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;

    bool has(const std::string& name) const { return options.count(name) != 0; }

    std::string get(const std::string& name, const std::string& fallback = {}) const
    {
        const auto found = options.find(name);
        return found == options.end() ? fallback : found->second;
    }
};

Args parseArgs(int argc, char** argv)
{
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string word = argv[i];
        if (word.rfind("--", 0) == 0) {
            const std::string name = word.substr(2);
            const bool takesValue = name == "view" || name == "size" || name == "highlight" || name == "at";
            args.options[name] = takesValue && i + 1 < argc ? argv[++i] : "";
        } else {
            args.positional.push_back(word);
        }
    }
    return args;
}

std::string readFile(const std::string& path)
{
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot read " + path);
    }
    std::ostringstream text;
    text << in.rdbuf();
    std::string source = text.str();
    source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
    return source;
}

void writeFile(const std::string& path, const std::string& text)
{
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    out << text;
    if (!out) {
        throw std::runtime_error("Cannot write " + path);
    }
}

void printDiagnostics(const std::vector<lang::Diagnostic>& diagnostics, const std::string& file)
{
    for (const lang::Diagnostic& d : diagnostics) {
        std::cout << lang::toText(d, file) << "\n";
    }
}

int commandFmt(const Args& args)
{
    const std::string& file = args.positional.at(1);
    const std::string source = readFile(file);
    const lang::ParseResult parsed = lang::parse(source);
    if (lang::hasErrors(parsed.diagnostics)) {
        printDiagnostics(parsed.diagnostics, file);
        return 1;
    }
    const std::string canonical = lang::format(parsed.document);
    if (args.has("check")) {
        if (canonical != source) {
            std::cout << file << " is not in canonical form\n";
            return 1;
        }
        return 0;
    }
    if (args.has("write")) {
        writeFile(file, canonical);
        return 0;
    }
    std::cout << canonical;
    return 0;
}

int commandCheck(const Args& args)
{
    const std::string& file = args.positional.at(1);
    const lang::ParseResult parsed = lang::parse(readFile(file));
    std::vector<lang::Diagnostic> diagnostics = parsed.diagnostics;
    if (!lang::hasErrors(diagnostics)) {
        const auto checked = lang::check(parsed.document);
        diagnostics.insert(diagnostics.end(), checked.begin(), checked.end());
    }
    if (args.has("json")) {
        std::cout << model::toJson(diagnostics).dump(2) << "\n";
    } else {
        printDiagnostics(diagnostics, file);
        if (diagnostics.empty()) {
            std::cout << file << ": " << parsed.document.statements.size() << " statements, no problems\n";
        }
    }
    return lang::hasErrors(diagnostics) ? 1 : 0;
}

int buildStatus(const model::Snapshot& snapshot)
{
    return lang::hasErrors(snapshot.diagnostics) ? 1 : 0;
}

model::Snapshot load(const Args& args, bool meshes)
{
    const std::string& file = args.positional.at(1);
    const rebuild::Build built = rebuild::run(readFile(file), {meshes && !args.has("at"), {}});
    if (!args.has("at")) {
        return built.snapshot;
    }
    model::Snapshot snapshot = rebuild::at(built, args.get("at"), meshes);
    snapshot.diagnostics = built.snapshot.diagnostics;
    return snapshot;
}

int commandBuild(const Args& args)
{
    const std::string& file = args.positional.at(1);
    const model::Snapshot snapshot = rebuild::build(readFile(file), {args.has("mesh"), {}});
    if (args.has("json")) {
        std::cout << model::toJson(snapshot, args.has("mesh")).dump(2) << "\n";
        return buildStatus(snapshot);
    }
    printDiagnostics(snapshot.diagnostics, file);
    std::cout << model::describe(snapshot) << "\n";
    std::cout << "rebuild " << lang::formatNumber(std::round(snapshot.totalMilliseconds * 10.0) / 10.0) << " ms\n";
    return buildStatus(snapshot);
}

int commandQuery(const Args& args)
{
    const std::string& file = args.positional.at(1);
    const std::string& text = args.positional.at(2);
    const model::Snapshot snapshot = load(args, false);
    if (!args.has("json")) {
        printDiagnostics(snapshot.diagnostics, file);
    }
    model::Selection selection;
    if (text == "*") {
        for (int i = 0; i < static_cast<int>(snapshot.faces.size()); ++i) {
            selection.items.push_back(i);
        }
    } else {
        const lang::ExpressionResult parsed = lang::parseExpression(text);
        if (!parsed.expr) {
            printDiagnostics(parsed.diagnostics, "selector");
            return 2;
        }
        try {
            selection = model::select(*parsed.expr, snapshot);
        } catch (const lang::EvalError& error) {
            if (args.has("json")) {
                std::cout << nlohmann::json{{"error", error.what()}, {"hint", error.hint}}.dump(2) << "\n";
            } else {
                std::cout << "error: " << error.what() << "\n";
                if (!error.hint.empty()) {
                    std::cout << "    hint: " << error.hint << "\n";
                }
            }
            return 1;
        }
    }
    if (args.has("json")) {
        nlohmann::json list = nlohmann::json::array();
        for (const int index : selection.items) {
            if (selection.edges) {
                const model::Edge& e = snapshot.edges[index];
                list.push_back({{"index", index}, {"selector", e.selector}, {"kind", e.kind}, {"length", e.length}});
            } else {
                list.push_back({{"index", index},
                                {"selector", snapshot.faces[index].selector},
                                {"text", model::describeFace(snapshot, index)}});
            }
        }
        std::cout << nlohmann::json{{"kind", selection.edges ? "edges" : "faces"},
                                    {"items", list},
                                    {"diagnostics", model::toJson(snapshot.diagnostics)}}
                         .dump(2)
                  << "\n";
        return 0;
    }
    for (const int index : selection.items) {
        if (selection.edges) {
            const model::Edge& e = snapshot.edges[index];
            std::cout << e.selector << ": " << e.kind << " edge, "
                      << lang::formatNumber(std::round(e.length * 100) / 100) << " mm\n";
        } else {
            std::cout << model::describeFace(snapshot, index) << "\n";
        }
    }
    return 0;
}

int commandDescribe(const Args& args)
{
    const std::string& file = args.positional.at(1);
    const model::Snapshot snapshot = load(args, false);
    printDiagnostics(snapshot.diagnostics, file);
    std::cout << model::describe(snapshot) << "\n";
    return buildStatus(snapshot);
}

int commandRender(const Args& args)
{
    const std::string& file = args.positional.at(1);
    const std::string& out = args.positional.at(2);
    const model::Snapshot snapshot = load(args, true);
    printDiagnostics(snapshot.diagnostics, file);
    const std::optional<look::View> view = look::namedView(args.get("view", "iso"));
    if (!view) {
        std::cerr << "Unknown view " << args.get("view") << "\n";
        return 2;
    }
    int width = 960;
    int height = 720;
    if (args.has("size")) {
        const std::string size = args.get("size");
        const char* end = size.data() + size.size();
        const auto [afterWidth, widthError] = std::from_chars(size.data(), end, width);
        const bool separated = widthError == std::errc() && afterWidth != end && *afterWidth == 'x';
        const auto [afterHeight, heightError] = separated ? std::from_chars(afterWidth + 1, end, height)
                                                          : std::from_chars_result{end, std::errc::invalid_argument};
        if (!separated || heightError != std::errc() || afterHeight != end || width <= 0 || height <= 0) {
            std::cerr << "Size reads like 960x720\n";
            return 2;
        }
    }
    look::Style style;
    if (args.has("highlight")) {
        const lang::ExpressionResult parsed = lang::parseExpression(args.get("highlight"));
        if (parsed.expr) {
            try {
                const model::Selection selection = model::select(*parsed.expr, snapshot);
                if (!selection.edges) {
                    style.highlighted.insert(selection.items.begin(), selection.items.end());
                }
            } catch (const lang::EvalError& error) {
                std::cerr << "highlight: " << error.what() << "\n";
            }
        }
    }
    constexpr int kSupersample = 2;
    const look::Frame frame = look::render(snapshot, *view, width * kSupersample, height * kSupersample, style);
    QImage image = look::toImage(frame);
    if (args.has("labels")) {
        look::drawLabels(image, frame, snapshot, kSupersample);
    }
    std::string caption = snapshot.part.empty() ? file : snapshot.part;
    if (!snapshot.at.empty()) {
        caption += "   at " + snapshot.at;
    } else if (snapshot.built) {
        int passed = 0;
        for (const auto& check : snapshot.checks) {
            passed += check.passed ? 1 : 0;
        }
        caption += "   " + lang::formatNumber(std::round(snapshot.volume)) + " mm3   checks " + std::to_string(passed) +
                   "/" + std::to_string(snapshot.checks.size());
    }
    look::drawCaption(image, QString::fromStdString(caption), kSupersample);
    image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (!image.save(QString::fromStdString(out))) {
        std::cerr << "Cannot write " << out << "\n";
        return 1;
    }
    return buildStatus(snapshot);
}

int commandExport(const Args& args)
{
    rebuild::exportStep(readFile(args.positional.at(1)), std::filesystem::u8path(args.positional.at(2)));
    return 0;
}

int commandSet(const Args& args)
{
    const std::string& file = args.positional.at(1);
    writeFile(file, lang::setParam(readFile(file), args.positional.at(2), args.positional.at(3)));
    return 0;
}

int commandSettle(const Args& args)
{
    const std::string& file = args.positional.at(1);
    const std::string source = readFile(file);
    const model::Snapshot snapshot = rebuild::build(source, {false, {}});
    std::vector<lang::Diagnostic> stops;
    for (const lang::Diagnostic& d : snapshot.diagnostics) {
        if (d.severity == lang::Diagnostic::Severity::Error && d.code != "check") {
            stops.push_back(d);
        }
    }
    if (!stops.empty()) {
        printDiagnostics(stops, file);
        std::cout << "Nothing settled, since the sketches must solve first\n";
        return 1;
    }
    lang::SketchPositions solved;
    for (const model::SolvedSketch& sketch : snapshot.sketches) {
        for (const model::SolvedPoint& p : sketch.points) {
            solved[sketch.name][p.name] = {p.u, p.v};
        }
    }
    const std::string settled = lang::settleGuesses(source, solved);
    if (args.has("check")) {
        const lang::ParseResult written = lang::parse(source);
        int off = 0;
        for (const lang::Statement& s : written.document.statements) {
            if (s.kind != lang::Statement::Kind::Sketch || !solved.contains(s.name)) {
                continue;
            }
            for (const lang::SketchItem& item : s.items) {
                const auto at = solved[s.name].find(item.name);
                if (item.kind != lang::SketchItem::Kind::Point || !item.center.x || at == solved[s.name].end()) {
                    continue;
                }
                const std::string guess =
                    "(" + lang::format(*item.center.x) + ", " + lang::format(*item.center.y) + ")";
                const std::string place =
                    "(" + lang::formatLength(at->second.first) + ", " + lang::formatLength(at->second.second) + ")";
                if (guess != place) {
                    std::cout << s.name << "." << item.name << " is guessed near " << guess << " and solves to "
                              << place << "\n";
                    ++off;
                }
            }
        }
        if (off > 0) {
            std::cout << off << " guess" << (off == 1 ? "" : "es") << " differ from the solution. Run kalvec settle "
                      << file << " --write to settle them\n";
            return 1;
        }
        return 0;
    }
    if (args.has("write")) {
        writeFile(file, settled);
        return 0;
    }
    std::cout << settled;
    return 0;
}

int commandSuggest(const Args& args)
{
    const std::string& file = args.positional.at(1);
    const model::Snapshot current = rebuild::build(readFile(file), {false, {}});
    if (!lang::hasErrors(current.diagnostics)) {
        std::cout << (args.has("json") ? "[]" : "Every check passes, nothing to suggest") << "\n";
        return 0;
    }
    const std::vector<rebuild::Suggestion> suggestions = rebuild::suggest(readFile(file));
    if (args.has("json")) {
        nlohmann::json list = nlohmann::json::array();
        for (const rebuild::Suggestion& s : suggestions) {
            list.push_back({{"check", s.check},
                            {"param", s.param},
                            {"from", s.from},
                            {"to", s.to},
                            {"value", s.value},
                            {"all_checks_pass", s.allChecksPass},
                            {"builds", s.builds}});
        }
        std::cout << list.dump(2) << "\n";
    } else if (suggestions.empty()) {
        std::cout << "No single param change fixes a failed check without other errors\n";
    } else {
        for (const rebuild::Suggestion& s : suggestions) {
            std::cout << "check " << s.check << ": set " << s.param << " = " << s.value << " (was "
                      << lang::formatNumber(std::round(s.from * 1000.0) / 1000.0) << " mm)"
                      << (s.allChecksPass ? ", every check passes" : ", fixes this check but breaks another")
                      << ", proven with " << s.builds << " rebuilds\n";
        }
    }
    if (args.has("apply") && !suggestions.empty() && suggestions.front().allChecksPass) {
        writeFile(file, suggestions.front().source);
        std::cout << "applied " << suggestions.front().param << " = " << suggestions.front().value << "\n";
    }
    return suggestions.empty() ? 1 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    const Args args = parseArgs(argc, argv);
    if (args.positional.size() == 1 && args.positional[0] == "help") {
        std::cout << lang::reference();
        return 0;
    }
    if (args.positional.size() < 2) {
        std::cout << kUsage;
        return 2;
    }
    const std::string& command = args.positional[0];
    const std::map<std::string, std::pair<std::size_t, int (*)(const Args&)>> commands = {
        {"fmt", {2, commandFmt}},       {"check", {2, commandCheck}},       {"build", {2, commandBuild}},
        {"query", {3, commandQuery}},   {"describe", {2, commandDescribe}}, {"render", {3, commandRender}},
        {"export", {3, commandExport}}, {"set", {4, commandSet}},           {"suggest", {2, commandSuggest}},
        {"settle", {2, commandSettle}},
    };
    const auto found = commands.find(command);
    if (found == commands.end() || args.positional.size() < found->second.first) {
        std::cout << kUsage;
        return 2;
    }
    std::unique_ptr<QGuiApplication> gui;
    if (command == "render") {
        static int fakeArgc = 1;
        static char name[] = "kalvec";
        static char* fakeArgv[] = {name, nullptr};
        qputenv("QT_QPA_PLATFORM", "offscreen");
        gui = std::make_unique<QGuiApplication>(fakeArgc, fakeArgv);
        look::loadFonts(QGuiApplication::applicationDirPath());
    }
    try {
        return found->second.second(args);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
