// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec2 corner;
layout(location = 1) in vec3 anchor;
layout(location = 2) in vec4 color;
layout(location = 3) in vec4 pick;
layout(location = 4) in vec3 sizes;
layout(location = 5) in vec4 style;

layout(location = 0) flat out uint id;
layout(location = 1) out vec2 depthAndW;
layout(location = 2) flat out vec4 mode;

layout(std140, binding = 0) uniform Frame
{
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

layout(std140, binding = 2) uniform Pick
{
    uvec4 pick_;
    vec4 widths;
};

const vec4 outside = vec4(2.0, 2.0, 2.0, 1.0);

uint byteOf(float value)
{
    return uint(round(value * 255.0));
}

void main()
{
    id = byteOf(pick.r) | (byteOf(pick.g) << 8) | (byteOf(pick.b) << 16) | (byteOf(pick.a) << 24);
    mode = vec4(overlay.w, 1.0, 0.0, 0.0);
    vec4 clip = viewProjection * vec4(anchor, 1.0);
    depthAndW = vec2(clip.z, clip.w);
    if (clip.w <= 0.0) {
        gl_Position = outside;
        depthAndW = vec2(0.0, 1.0);
        return;
    }
    vec2 size = target.xy;
    vec2 pixel = (clip.xy / clip.w * 0.5 + 0.5) * size;
    float side = floor(widths.x + 0.5);
    vec2 shift = mod(side, 2.0) < 0.5 ? vec2(-0.5, 0.5) : vec2(0.0);
    vec2 center = floor(pixel) + 0.5 + shift;
    vec2 offset = vec2(corner.x * 2.0 - 1.0, corner.y) * side * 0.5;
    vec2 ndc = (center + offset) / size * 2.0 - 1.0;
    gl_Position = vec4(ndc, overlay.w > 0.5 ? 0.5 : 0.0, 1.0);
}
