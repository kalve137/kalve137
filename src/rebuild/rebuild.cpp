// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rebuild/rebuild.h"

#include "lang/check.h"
#include "lang/format.h"
#include "lang/parse.h"
#include "model/compass.h"
#include "model/select.h"
#include "rebuild/outline.h"
#include "rebuild/sketch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace kalve137::rebuild {

namespace {

using lang::Diagnostic;
using lang::EvalError;
using lang::Expr;
using lang::Quantity;
using lang::Statement;

struct Body {
    kernel::Shape shape;
    std::vector<std::string> names;
};

Body named(const kernel::Result& result, std::initializer_list<const Body*> inputs, const std::string& fallback)
{
    const std::vector<const Body*> from(inputs);
    Body body{result.shape, {}};
    for (const kernel::Origin& origin : result.origins) {
        body.names.push_back(origin.input >= 0 && origin.face >= 0 ? from.at(origin.input)->names.at(origin.face)
                                                                   : fallback);
    }
    return body;
}

std::vector<int> splitFaces(const std::vector<std::string>& names)
{
    std::map<std::string, int> count;
    for (const std::string& name : names) {
        ++count[name];
    }
    std::vector<int> result;
    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
        if (count[names[i]] > 1) {
            result.push_back(i);
        }
    }
    return result;
}

void missed(const std::vector<Outline>& outlines, const Body& result, const Statement& cut, const Statement& sketch,
            const model::Frame& frame)
{
    for (std::size_t i = 0; i < outlines.size(); ++i) {
        const bool nested = std::ranges::any_of(outlines, [&](const Outline& other) {
            return &other != &outlines[i] && inside(other.loop, outlines[i].loop);
        });
        const std::string prefix = cut.name + "." + outlines[i].item + ".";
        const bool left =
            std::ranges::any_of(result.names, [&](const std::string& name) { return name.rfind(prefix, 0) == 0; });
        if (!nested && !left) {
            throw EvalError(cut.span,
                            "Cut '" + cut.name + "' removes nothing where '" + outlines[i].item + "' of sketch '" +
                                sketch.name + "' is, so it lies outside the body",
                            "Sketch '" + sketch.name + "' on " + lang::format(*sketch.plane) + " has its " +
                                model::describeAxes(frame) + ". kalvec describe FILE lists every sketch on a face",
                            "miss");
        }
    }
}

double since(std::chrono::steady_clock::time_point started)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

model::Snapshot table(const Body& body, const kernel::Detail& detail, int* solids = nullptr,
                      model::Timing* timing = nullptr)
{
    auto started = std::chrono::steady_clock::now();
    const kernel::Inspection inspection = kernel::inspect(body.shape, detail);
    if (timing != nullptr) {
        timing->inspectMilliseconds += since(started);
        started = std::chrono::steady_clock::now();
    }
    if (solids != nullptr) {
        *solids = inspection.solids;
    }
    model::Snapshot snapshot;
    snapshot.built = true;
    snapshot.volume = inspection.volume;
    snapshot.boxMin = inspection.boxMin;
    snapshot.boxMax = inspection.boxMax;
    for (std::size_t i = 0; i < inspection.faces.size(); ++i) {
        const kernel::FaceInfo& info = inspection.faces[i];
        model::Face face;
        face.name = body.names.at(i);
        face.madeBy = face.name.substr(0, face.name.find('.'));
        face.kind = info.kind;
        face.area = info.area;
        face.centroid = info.centroid;
        face.normal = info.normal;
        face.axis = info.axis;
        face.radius = info.radius;
        face.vertices = info.vertices;
        face.triangles = info.triangles;
        snapshot.faces.push_back(std::move(face));
    }
    for (const kernel::EdgeInfo& info : inspection.edges) {
        snapshot.edges.push_back({{}, info.faceA, info.faceB, info.kind, info.length, info.points});
    }
    model::assignSelectors(snapshot);
    if (timing != nullptr) {
        timing->nameMilliseconds += since(started);
    }
    return snapshot;
}

Body prismSketch(const std::vector<Outline>& outlines, const Statement& sketch, const model::Frame& frame,
                 model::Vec3 offset, model::Vec3 vector, const std::string& feature, const std::string& startName,
                 const std::string& endName)
{
    std::optional<Body> solid;
    std::vector<Body> holes;
    for (std::size_t i = 0; i < outlines.size(); ++i) {
        int depth = 0;
        for (std::size_t j = 0; j < outlines.size(); ++j) {
            if (j != i && inside(outlines[j].loop, outlines[i].loop)) {
                ++depth;
            }
        }
        const kernel::Prism prism = kernel::prism(frame, outlines[i].loop, offset, vector);
        const std::string prefix = feature + "." + outlines[i].item + ".";
        std::vector<std::string> names;
        names.reserve(prism.faces.size());
        for (const kernel::PrismFace& face : prism.faces) {
            switch (face.kind) {
            case kernel::PrismFace::Kind::Start:
                names.push_back(prefix + startName);
                break;
            case kernel::PrismFace::Kind::End:
                names.push_back(prefix + endName);
                break;
            case kernel::PrismFace::Kind::Side: {
                const std::string& side = outlines[i].sides.at(static_cast<std::size_t>(face.segment));
                names.push_back(prefix + (side.empty() ? model::compassWord(face.outward) : side));
                break;
            }
            }
        }
        Body body{prism.shape, std::move(names)};
        if (depth % 2 == 1) {
            holes.push_back(std::move(body));
        } else if (solid) {
            solid = named(kernel::join(solid->shape, body.shape), {&*solid, &body}, feature);
        } else {
            solid = std::move(body);
        }
    }
    if (!solid) {
        throw EvalError(sketch.span, "Sketch '" + sketch.name + "' has no outer item to make a solid from");
    }
    Body result = std::move(*solid);
    for (const Body& hole : holes) {
        result = named(kernel::cut(result.shape, hole.shape), {&result, &hole}, feature);
    }
    return result;
}

class Measurer : public lang::Measurer {
public:
    Measurer(const model::Snapshot& snapshot, const kernel::Shape& shape) : m_snapshot(snapshot), m_shape(shape) {}

    Quantity measure(const Expr& call, const lang::Params&) override
    {
        if (call.text == "volume") {
            return {m_snapshot.volume, {3, 0}};
        }
        if (call.text == "size_x" || call.text == "size_y" || call.text == "size_z") {
            const model::Vec3 size = m_snapshot.boxMax - m_snapshot.boxMin;
            const double value = call.text == "size_x" ? size.x : (call.text == "size_y" ? size.y : size.z);
            return {value, {1, 0}};
        }
        if (call.text == "count") {
            try {
                return {static_cast<double>(model::select(*call.args[0], m_snapshot).items.size()), {}};
            } catch (const EvalError&) {
                return {0.0, {}};
            }
        }
        if (call.text == "area") {
            double total = 0.0;
            for (const int index : model::selectUse(*call.args[0], m_snapshot, "area()").items) {
                total += m_snapshot.faces[index].area;
            }
            return {total, {2, 0}};
        }
        if (call.text == "distance") {
            const model::Selection a = model::selectUse(*call.args[0], m_snapshot, "distance()");
            const model::Selection b = model::selectUse(*call.args[1], m_snapshot, "distance()");
            return {kernel::distance(m_shape, {a.edges, a.items}, {b.edges, b.items}), {1, 0}};
        }
        throw EvalError(call.span, "Unknown measurement " + call.text);
    }

private:
    const model::Snapshot& m_snapshot;
    const kernel::Shape& m_shape;
};

bool holds(double lhs, const std::string& comparison, double rhs)
{
    const double tolerance = 1e-9 * std::max(1.0, std::abs(rhs));
    if (comparison == ">=") {
        return lhs >= rhs - tolerance;
    }
    if (comparison == "<=") {
        return lhs <= rhs + tolerance;
    }
    if (comparison == ">") {
        return lhs > rhs;
    }
    if (comparison == "<") {
        return lhs < rhs;
    }
    return std::abs(lhs - rhs) <= 1e-6 * std::max(1.0, std::abs(rhs));
}

void runStatements(const std::string& source, const Options& options, Build& out)
{
    using Clock = std::chrono::steady_clock;
    model::Snapshot& snapshot = out.snapshot;
    lang::ParseResult parsed = lang::parse(source);
    snapshot.diagnostics = parsed.diagnostics;
    if (!lang::hasErrors(parsed.diagnostics)) {
        const std::vector<Diagnostic> checked = lang::check(parsed.document);
        snapshot.diagnostics.insert(snapshot.diagnostics.end(), checked.begin(), checked.end());
    }
    if (lang::hasErrors(snapshot.diagnostics)) {
        return;
    }
    const lang::Document& document = parsed.document;

    lang::Params params;
    std::optional<Body> body;
    std::optional<model::Snapshot> current;
    bool measured = false;
    std::map<std::string, model::Frame> frames;
    std::map<std::string, std::vector<Outline>> outlines;
    bool stopped = false;
    for (std::size_t index = 0; index < document.statements.size() && !stopped; ++index) {
        const Statement& s = document.statements[index];
        out.stages.push_back(
            {s.name, body ? body->shape : kernel::Shape(), body ? body->names : std::vector<std::string>()});
        const auto stepStarted = Clock::now();
        model::Timing timing{s.name.empty() ? lang::toString(s.kind) : s.name};
        const auto view = [&](bool measures) -> const model::Snapshot& {
            if (!body) {
                throw EvalError(s.span, "There is no body yet for " + s.name, "Start with an extrude");
            }
            if (!current || (measures && !measured)) {
                current = table(*body, {measures, false, splitFaces(body->names)}, nullptr, &timing);
                measured = measures;
            }
            return *current;
        };
        try {
            switch (s.kind) {
            case Statement::Kind::Part:
                snapshot.part = s.name;
                break;
            case Statement::Kind::Param: {
                Quantity value = lang::evaluate(*s.value, params);
                const auto override = options.overrides.find(s.name);
                if (override != options.overrides.end()) {
                    value.value = override->second;
                }
                params[s.name] = value;
                snapshot.params[s.name] = value;
                break;
            }
            case Statement::Kind::Sketch: {
                if (s.plane->kind == Expr::Kind::Name && lang::isPlaneName(s.plane->text)) {
                    frames[s.name] = *model::datumFrame(s.plane->text);
                } else {
                    if (!body) {
                        throw EvalError(s.plane->span, "There is no body yet to put sketch '" + s.name + "' on",
                                        "Start on XY, XZ or YZ");
                    }
                    const int face = model::selectOneFace(*s.plane, view(false), "A sketch plane");
                    const std::optional<model::Frame> frame = kernel::faceFrame(body->shape, face);
                    if (!frame) {
                        throw EvalError(s.plane->span, "Face " + current->faces[face].selector +
                                                           " is not flat, so a sketch cannot sit on it");
                    }
                    frames[s.name] = *frame;
                    snapshot.placements.push_back({s.name, lang::format(*s.plane), *frame});
                }
                outlines[s.name] = sketchOutlines(s, params, snapshot.diagnostics, snapshot.sketches);
                break;
            }
            case Statement::Kind::Extrude: {
                const Statement& sketch = *lang::sketchOf(document, s);
                const model::Frame& frame = frames.at(sketch.name);
                const double distance = lengthOf(*s.value, params, "extrude distance", true);
                Body solid = prismSketch(outlines.at(sketch.name), sketch, frame, {},
                                         frame.normal * (s.down ? -distance : distance), s.name, "start", "end");
                body =
                    body ? named(kernel::join(body->shape, solid.shape), {&*body, &solid}, s.name) : std::move(solid);
                break;
            }
            case Statement::Kind::Cut: {
                const Statement& sketch = *lang::sketchOf(document, s);
                const model::Frame& frame = frames.at(sketch.name);
                model::Vec3 into = frame.normal * -1.0;
                if (!body) {
                    throw EvalError(s.span, "There is no body yet for cut '" + s.name + "' to cut",
                                    "Start with an extrude");
                }
                if (frame.datum) {
                    const model::Snapshot& seen = view(false);
                    const model::Vec3 middle = (seen.boxMin + seen.boxMax) * 0.5;
                    into = model::dot(middle - frame.origin, frame.normal) >= 0.0 ? frame.normal : frame.normal * -1.0;
                }
                model::Vec3 offset;
                double depth = 0.0;
                if (s.through) {
                    const model::Snapshot& seen = view(false);
                    const model::Vec3 size = seen.boxMax - seen.boxMin;
                    depth = 2.0 * std::sqrt(model::dot(size, size)) + 10.0;
                    if (frame.datum) {
                        offset = into * -depth;
                        depth *= 2.0;
                    }
                } else {
                    depth = lengthOf(*s.value, params, "cut depth", true);
                }
                const Body tool = prismSketch(outlines.at(sketch.name), sketch, frame, offset, into * depth, s.name,
                                              "mouth", "floor");
                Body next = named(kernel::cut(body->shape, tool.shape), {&*body, &tool}, s.name);
                if (next.shape.faceCount() == 0) {
                    throw EvalError(s.span, "Cut '" + s.name + "' removes the whole body");
                }
                missed(outlines.at(sketch.name), next, s, sketch, frame);
                body = std::move(next);
                break;
            }
            case Statement::Kind::Fillet: {
                const model::Selection edges = model::selectUse(*s.lhs, view(false), "A fillet");
                const double radius = lengthOf(*s.value, params, "fillet radius", true);
                kernel::Result rounded;
                try {
                    rounded = kernel::round(body->shape, edges.items, radius);
                } catch (const kernel::KernelError&) {
                    throw EvalError(s.value->span,
                                    "The kernel could not round " + std::to_string(edges.items.size()) +
                                        " edges at radius " + lang::toText(Quantity{radius, {1, 0}}),
                                    "Try a smaller radius or fewer edges");
                }
                Body next{rounded.shape, {}};
                for (const kernel::Origin& origin : rounded.origins) {
                    next.names.push_back(origin.input == 0 && origin.edge < 0 ? body->names.at(origin.face)
                                                                              : s.name + ".round");
                }
                body = std::move(next);
                break;
            }
            case Statement::Kind::Check: {
                if (!body) {
                    throw EvalError(s.span, "There is no body yet to check");
                }
                Measurer measurer(view(true), body->shape);
                const Quantity lhs = lang::evaluate(*s.lhs, params, &measurer);
                const Quantity rhs = lang::evaluate(*s.rhs, params, &measurer);
                model::CheckResult result{s.name,
                                          s.span,
                                          holds(lhs.value, s.comparison, rhs.value),
                                          lhs,
                                          rhs,
                                          s.comparison,
                                          lang::format(*s.lhs)};
                if (!result.passed) {
                    const double gap = std::abs(lhs.value - rhs.value);
                    snapshot.diagnostics.push_back({Diagnostic::Severity::Error, "check", s.span, s.name,
                                                    result.expression + " is " + lang::toText(lhs) + " but must be " +
                                                        s.comparison + " " + lang::toText(rhs),
                                                    "Off by " + lang::toText(Quantity{gap, lhs.dim}) +
                                                        ". Run kalvec suggest FILE for params "
                                                        "that would fix it"});
                }
                snapshot.checks.push_back(std::move(result));
                break;
            }
            }
            if (s.kind == Statement::Kind::Extrude || s.kind == Statement::Kind::Cut ||
                s.kind == Statement::Kind::Fillet) {
                current.reset();
                measured = false;
            }
        } catch (const EvalError& error) {
            snapshot.diagnostics.push_back({Diagnostic::Severity::Error, error.code,
                                            error.span.line > 0 ? error.span : s.span, s.name, error.what(),
                                            error.hint});
            stopped = true;
        } catch (const std::exception& error) {
            snapshot.diagnostics.push_back({Diagnostic::Severity::Error, "kernel", s.span, s.name, error.what(), {}});
            stopped = true;
        }
        timing.milliseconds = since(stepStarted);
        snapshot.timings.push_back(timing);
        if (stopped && index + 1 < document.statements.size()) {
            snapshot.diagnostics.push_back(
                {Diagnostic::Severity::Note,
                 "stopped",
                 s.span,
                 s.name,
                 std::to_string(document.statements.size() - index - 1) + " statements after this one were not built",
                 {}});
        }
    }
    if (body) {
        int solids = 0;
        const auto finalStarted = Clock::now();
        model::Timing timing{"final table"};
        model::Snapshot final = table(*body, {true, options.meshes, {}}, &solids, &timing);
        timing.milliseconds = since(finalStarted);
        snapshot.timings.push_back(timing);
        if (solids > 1) {
            snapshot.diagnostics.push_back(
                {Diagnostic::Severity::Warning,
                 "pieces",
                 {},
                 {},
                 "The body is " + std::to_string(solids) + " separate solids, and a part should be one",
                 "Make sure each extrude touches the body"});
        }
        snapshot.faces = std::move(final.faces);
        snapshot.edges = std::move(final.edges);
        snapshot.volume = final.volume;
        snapshot.boxMin = final.boxMin;
        snapshot.boxMax = final.boxMax;
        snapshot.built = true;
        out.shape = body->shape;
    }
    snapshot.complete = !stopped;
}

} // namespace

Build run(const std::string& source, const Options& options)
{
    const auto started = std::chrono::steady_clock::now();
    Build out;
    try {
        runStatements(source, options, out);
    } catch (const std::exception& error) {
        out.snapshot.complete = false;
        out.snapshot.diagnostics.push_back({Diagnostic::Severity::Error,
                                            "internal",
                                            {},
                                            {},
                                            std::string("The rebuild stopped on an internal error: ") + error.what(),
                                            "The source is fine to keep editing. Please report this file"});
    } catch (...) {
        out.snapshot.complete = false;
        out.snapshot.diagnostics.push_back({Diagnostic::Severity::Error,
                                            "internal",
                                            {},
                                            {},
                                            "The rebuild stopped on an unknown internal error",
                                            "The source is fine to keep editing. Please report this file"});
    }
    out.snapshot.totalMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return out;
}

model::Snapshot build(const std::string& source, const Options& options)
{
    return run(source, options).snapshot;
}

model::Snapshot at(const Build& build, const std::string& statement, bool meshes)
{
    std::string names;
    for (const Stage& stage : build.stages) {
        if (stage.statement != statement) {
            if (!stage.statement.empty()) {
                names += (names.empty() ? "" : ", ") + stage.statement;
            }
            continue;
        }
        model::Snapshot snapshot =
            stage.shape.empty() ? model::Snapshot() : table({stage.shape, stage.names}, {true, meshes, {}});
        snapshot.part = build.snapshot.part;
        snapshot.at = statement;
        return snapshot;
    }
    throw std::invalid_argument("No statement named '" + statement + "' was built. Built statements are " + names);
}

void exportStep(const std::string& source, const std::filesystem::path& path)
{
    const Build built = run(source, {false, {}});
    if (built.shape.empty()) {
        throw std::runtime_error("There is no body to export");
    }
    kernel::exportStep(built.shape, built.snapshot.part.empty() ? "part" : built.snapshot.part, path);
}

} // namespace kalve137::rebuild
