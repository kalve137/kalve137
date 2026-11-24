// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec2 corner;
layout(location = 1) in vec3 anchor;
layout(location = 2) in vec2 offset;
layout(location = 3) in vec4 box;
layout(location = 4) in vec4 color;
layout(location = 5) in vec4 border;
layout(location = 6) in vec4 style;
layout(location = 7) in vec2 shape;

layout(location = 0) out vec2 local;
layout(location = 1) flat out vec4 fill;
layout(location = 2) flat out vec4 edge;
layout(location = 3) flat out vec4 frame;
layout(location = 4) flat out vec4 panel;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 eye;
    vec4 eyePosition;
    vec4 light;
    vec4 look;
    vec4 specular;
    vec4 options;
    vec4 edge_;
    vec4 target;
    vec4 gold;
    vec4 steps;
    vec4 hidden;
    mat4 wideViewProjection;
    vec4 overlay;
};

const vec4 outside = vec4(2.0, 2.0, 2.0, 1.0);

void main()
{
    fill = color;
    edge = border;
    frame = box;
    panel = vec4(floor(style.x * 255.0 + 0.5), shape, 0.0);
    vec4 clip = viewProjection * vec4(anchor, 1.0);
    if (clip.w <= 0.0) {
        gl_Position = outside;
        local = vec2(0.0);
        return;
    }
    float up = options.w > 0.5 ? 1.0 : -1.0;
    vec2 ndc = clip.xy / clip.w;
    vec2 screen = vec2(ndc.x * 0.5 + 0.5, 0.5 - up * ndc.y * 0.5) * target.xy;
    vec2 unit = vec2(corner.x, corner.y * 0.5 + 0.5);
    local = unit * box.xy;
    vec2 pixel = floor(screen + 0.5) + offset + local;
    vec2 placed = vec2(pixel.x / target.x * 2.0 - 1.0, up * (1.0 - pixel.y / target.y * 2.0));
    gl_Position = vec4(placed, overlay.w > 0.5 ? 0.5 : 0.0, 1.0);
}
