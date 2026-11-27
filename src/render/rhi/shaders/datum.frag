// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec4 linearColor;
layout(location = 1) in vec2 depthAndW;
layout(location = 2) flat in vec2 bias;
layout(location = 3) flat in vec2 dash;
layout(location = 4) in vec2 fromEndAndW;

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
    mat4 wideViewProjection;
    vec4 overlay;
    vec4 overlayBias;
    vec4 overlayScale;
    vec4 overlayPlane;
};

vec3 toSrgb(vec3 linear)
{
    vec3 value = clamp(linear, 0.0, 1.0);
    vec3 low = value * 12.92;
    vec3 high = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), value));
}

void main()
{
    float depth = depthAndW.x / depthAndW.y;
    if (overlay.w < 0.5) {
        depth = depth * 0.5 + 0.5;
    }
    float slope = max(abs(dFdx(depth)), abs(dFdy(depth)));
    float row = options.z > 0.5 ? gl_FragCoord.y : target.y - gl_FragCoord.y;
    float at = dash.x > 0.5 ? gl_FragCoord.x : row;
    uint bit = uint(floor(at / overlayScale.y + 0.5)) & 15u;
    if (overlayScale.w > 0.5) {
        bit = 15u - (uint(floor(fromEndAndW.x / fromEndAndW.y / overlayScale.y)) & 15u);
    }
    if ((uint(dash.y) & (1u << bit)) == 0u) {
        discard;
    }
    gl_FragDepth = clamp(depth + slope * bias.x + bias.y, 0.0, 1.0);
    fragColor = vec4(toSrgb(linearColor.rgb), linearColor.a);
}
