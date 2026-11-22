// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 linearColor;
layout(location = 1) out vec2 depthAndW;
layout(location = 2) flat out vec2 bias;

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

vec3 toLinear(vec3 encoded)
{
    vec3 low = encoded / 12.92;
    vec3 high = pow((encoded + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), encoded));
}

void main()
{
    linearColor = vec4(toLinear(color.rgb), color.a);
    bias = overlayBias.zw;
    vec4 body = viewProjection * vec4(position, 1.0);
    depthAndW = vec2(body.z, body.w);
    gl_Position = wideViewProjection * vec4(position, 1.0);
}
