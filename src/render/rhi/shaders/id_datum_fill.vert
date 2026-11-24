// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

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
    uvec4 pick;
    vec4 widths;
};

void main()
{
    uint entry = uint(round(color.r * 255.0)) + uint(round(color.g * 255.0)) * 256u;
    id = pick.x | (uint(round(color.b * 255.0)) << 16) | entry;
    vec4 body = viewProjection * vec4(position, 1.0);
    depthAndW = vec2(body.z, body.w);
    mode = vec4(overlay.w, float(pick.y), overlayBias.zw);
    gl_Position = wideViewProjection * vec4(position, 1.0);
}
