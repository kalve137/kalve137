// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec3 band;
layout(location = 1) flat in float lengthPixels;

layout(location = 0) out vec4 fragColor;

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
};

void main()
{
    float across = band.x / band.z;
    float along = band.y / band.z;
    float past = max(max(-along, along - lengthPixels), 0.0);
    float outside = max(length(vec2(across, past)) - target.w * 0.5, 0.0);
    float strength = steps.z * clamp(1.0 - outside / max(steps.w, 1e-3), 0.0, 1.0);
    fragColor = vec4(0.0, 0.0, 0.0, 1.0 - strength);
}
