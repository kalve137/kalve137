// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 normalAndEntry;

layout(location = 0) flat out uint id;
layout(location = 1) out vec2 depthAndW;

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
};

layout(binding = 1) uniform sampler2D palette;

layout(std140, binding = 2) uniform Pick {
    uvec4 pick;
    vec4 widths;
};

void main()
{
    int entry = int(round(normalAndEntry.z * 255.0)) + int(round(normalAndEntry.w * 255.0)) * 256;
    vec4 value = texelFetch(palette, ivec2(entry % 256, entry / 256), 0);
    id = pick.x | uint(entry);
    depthAndW = vec2(0.0, 1.0);
    if ((int(round(value.a * 255.0)) & 1) == 0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    gl_Position = viewProjection * vec4(position, 1.0);
    depthAndW = vec2(widths.y > 0.5 ? gl_Position.z : (gl_Position.z + gl_Position.w) * 0.5, gl_Position.w);
}
