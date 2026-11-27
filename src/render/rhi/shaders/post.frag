// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

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

layout(binding = 1) uniform sampler2D frame;

vec3 pushAway(vec3 color, float amount)
{
    float toward = dot(color, vec3(0.2126, 0.7152, 0.0722)) >= steps.y ? 0.0 : 1.0;
    return mix(color, vec3(toward), amount);
}

void main()
{
    vec4 value = texelFetch(frame, ivec2(gl_FragCoord.xy), 0);
    fragColor = vec4(pushAway(value.rgb, 1.0 - value.a), 1.0);
}
