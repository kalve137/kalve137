// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec2 local;
layout(location = 1) flat in vec4 fillColor;
layout(location = 2) flat in vec4 outlineColor;
layout(location = 3) flat in vec4 glowColor;
layout(location = 4) flat in vec4 shape;
layout(location = 5) flat in vec2 widths;

layout(location = 0) out vec4 fragColor;

float box(vec2 p, vec2 center, vec2 half_)
{
    vec2 d = abs(p - center) - half_;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float triangle(vec2 p, vec2 p0, vec2 p1, vec2 p2)
{
    vec2 e0 = p1 - p0;
    vec2 e1 = p2 - p1;
    vec2 e2 = p0 - p2;
    vec2 v0 = p - p0;
    vec2 v1 = p - p1;
    vec2 v2 = p - p2;
    vec2 q0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 q1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 q2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(q0, q0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(q1, q1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(q2, q2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

vec4 over(vec4 top, vec4 below)
{
    return top + below * (1.0 - top.a);
}

void main()
{
    float shaft = shape.x;
    float head = shape.z;
    float reach = shaft + head / 3.0;
    float body = box(local, vec2(reach * 0.5, 0.0), vec2(reach * 0.5, shape.y * 0.5));
    float tip = triangle(local, vec2(shaft, -shape.w * 0.5), vec2(shaft + head, 0.0), vec2(shaft, shape.w * 0.5));
    float d = min(body, tip);
    float glowCover = glowColor.a * clamp(0.5 + widths.y * 0.5 - abs(d), 0.0, 1.0);
    float fillCover = fillColor.a * clamp(0.5 - d, 0.0, 1.0);
    float outlineCover = outlineColor.a * clamp(0.5 + widths.x * 0.5 - abs(d), 0.0, 1.0);
    vec4 color = vec4(glowColor.rgb * glowCover, glowCover);
    color = over(vec4(fillColor.rgb * fillCover, fillCover), color);
    color = over(vec4(outlineColor.rgb * outlineCover, outlineCover), color);
    if (color.a <= 0.0) {
        discard;
    }
    fragColor = vec4(color.rgb / color.a, color.a);
}
