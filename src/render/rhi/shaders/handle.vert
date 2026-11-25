// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec2 corner;
layout(location = 1) in vec3 anchor;
layout(location = 2) in vec3 toward;
layout(location = 3) in vec4 fill;
layout(location = 4) in vec4 outline;
layout(location = 5) in vec4 glow;
layout(location = 6) in vec4 arrow;
layout(location = 7) in vec2 stroke;

layout(location = 0) out vec2 local;
layout(location = 1) flat out vec4 fillColor;
layout(location = 2) flat out vec4 outlineColor;
layout(location = 3) flat out vec4 glowColor;
layout(location = 4) flat out vec4 shape;
layout(location = 5) flat out vec2 widths;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 eye;
    vec4 eyePosition;
    vec4 light;
    vec4 look;
    vec4 specular;
    vec4 options;
    vec4 edge;
    vec4 target;
    vec4 gold;
    vec4 steps;
    vec4 hidden;
    mat4 wideViewProjection;
    vec4 overlay;
    vec4 overlayBias;
    vec4 overlayScale;
    vec4 overlayPlane;
};

const vec4 outside = vec4(2.0, 2.0, 2.0, 1.0);
const float shortest = 0.001;

void main()
{
    float ratio = overlayScale.x;
    fillColor = fill;
    outlineColor = outline;
    glowColor = glow;
    shape = arrow * ratio;
    widths = stroke * ratio;
    local = vec2(0.0);
    vec4 from = viewProjection * vec4(anchor, 1.0);
    vec4 to = viewProjection * vec4(toward, 1.0);
    if (from.w <= 0.0 || to.w <= 0.0) {
        gl_Position = outside;
        return;
    }
    vec2 size = target.xy;
    vec2 start = (from.xy / from.w * 0.5 + 0.5) * size;
    vec2 end = (to.xy / to.w * 0.5 + 0.5) * size;
    float pixels = length(end - start);
    if (pixels < shortest * ratio) {
        gl_Position = outside;
        return;
    }
    vec2 along = (end - start) / pixels;
    vec2 across = vec2(-along.y, along.x);
    float pad = max(widths.x, widths.y) * 0.5 + 1.0;
    float x = mix(-pad, shape.x + shape.z + pad, corner.x);
    float y = corner.y * (shape.w * 0.5 + pad);
    local = vec2(x, y);
    vec2 ndc = (start + along * x + across * y) / size * 2.0 - 1.0;
    gl_Position = vec4(ndc, overlay.w > 0.5 ? 0.5 : 0.0, 1.0);
}
