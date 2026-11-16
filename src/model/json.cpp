// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "model/json.h"

#include "lang/check.h"

namespace kalve137::model {

namespace {

nlohmann::json vec(Vec3 v)
{
    return {v.x, v.y, v.z};
}

nlohmann::json quantity(const lang::Quantity& q)
{
    return {{"value", q.value}, {"unit", lang::toString(q.dim)}, {"text", lang::toText(q)}};
}

} // namespace

nlohmann::json toJson(const lang::Diagnostic& d)
{
    return {{"severity", lang::toString(d.severity)},
            {"code", d.code},
            {"line", d.span.line},
            {"column", d.span.column},
            {"statement", d.statement},
            {"message", d.message},
            {"hint", d.hint}};
}

nlohmann::json toJson(const std::vector<lang::Diagnostic>& diagnostics)
{
    nlohmann::json list = nlohmann::json::array();
    for (const lang::Diagnostic& d : diagnostics) {
        list.push_back(toJson(d));
    }
    return list;
}

nlohmann::json toJson(const Snapshot& snapshot, bool meshes)
{
    nlohmann::json faces = nlohmann::json::array();
    for (std::size_t i = 0; i < snapshot.faces.size(); ++i) {
        const Face& face = snapshot.faces[i];
        nlohmann::json f = {{"index", i},
                            {"selector", face.selector},
                            {"name", face.name},
                            {"made_by", face.madeBy},
                            {"kind", face.kind},
                            {"area", face.area},
                            {"centroid", vec(face.centroid)}};
        if (face.kind == "plane") {
            f["normal"] = vec(face.normal);
        }
        if (face.kind == "cylinder") {
            f["axis"] = vec(face.axis);
            f["radius"] = face.radius;
        }
        if (meshes) {
            nlohmann::json vertices = nlohmann::json::array();
            for (const Vec3& v : face.vertices) {
                vertices.push_back(vec(v));
            }
            f["vertices"] = vertices;
            f["triangles"] = face.triangles;
        }
        faces.push_back(f);
    }
    nlohmann::json edges = nlohmann::json::array();
    for (std::size_t i = 0; i < snapshot.edges.size(); ++i) {
        const Edge& edge = snapshot.edges[i];
        nlohmann::json e = {{"index", i},
                            {"selector", edge.selector},
                            {"faces", {edge.faceA, edge.faceB}},
                            {"kind", edge.kind},
                            {"length", edge.length}};
        if (meshes) {
            nlohmann::json points = nlohmann::json::array();
            for (const Vec3& p : edge.points) {
                points.push_back(vec(p));
            }
            e["points"] = points;
        }
        edges.push_back(e);
    }
    nlohmann::json checks = nlohmann::json::array();
    for (const CheckResult& c : snapshot.checks) {
        checks.push_back({{"name", c.name},
                          {"passed", c.passed},
                          {"line", c.span.line},
                          {"expression", c.expression},
                          {"comparison", c.comparison},
                          {"value", quantity(c.lhs)},
                          {"limit", quantity(c.rhs)}});
    }
    nlohmann::json params = nlohmann::json::object();
    for (const auto& [name, q] : snapshot.params) {
        params[name] = quantity(q);
    }
    nlohmann::json sketches = nlohmann::json::array();
    for (const SolvedSketch& s : snapshot.sketches) {
        nlohmann::json points = nlohmann::json::array();
        for (const SolvedPoint& p : s.points) {
            points.push_back({{"name", p.name}, {"u", p.u}, {"v", p.v}, {"free", p.free}});
        }
        nlohmann::json lines = nlohmann::json::array();
        for (const SolvedLine& l : s.lines) {
            lines.push_back({{"name", l.name}, {"from", l.from}, {"to", l.to}, {"length", l.length}});
        }
        sketches.push_back({{"name", s.name}, {"freedom", s.freedom}, {"points", points}, {"lines", lines}});
    }
    nlohmann::json timings = nlohmann::json::array();
    for (const Timing& t : snapshot.timings) {
        timings.push_back({{"statement", t.statement}, {"ms", t.milliseconds}});
    }
    return {{"part", snapshot.part},
            {"at", snapshot.at},
            {"built", snapshot.built},
            {"complete", snapshot.complete},
            {"volume", snapshot.volume},
            {"box", {{"min", vec(snapshot.boxMin)}, {"max", vec(snapshot.boxMax)}}},
            {"params", params},
            {"faces", faces},
            {"edges", edges},
            {"checks", checks},
            {"sketches", sketches},
            {"diagnostics", toJson(snapshot.diagnostics)},
            {"timings", timings},
            {"total_ms", snapshot.totalMilliseconds}};
}

} // namespace kalve137::model
