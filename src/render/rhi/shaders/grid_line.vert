// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec2 corner;
layout(location = 1) in vec3 start;
layout(location = 2) in vec3 end;
layout(location = 3) in vec4 startColor;
layout(location = 4) in vec4 endColor;
layout(location = 5) in float width;

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

const float shortest = 1e-9;
const vec4 outside = vec4(2.0, 2.0, 2.0, 1.0);

vec3 toLinear(vec3 encoded)
{
    vec3 low = encoded / 12.92;
    vec3 high = pow((encoded + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), encoded));
}

float nearDistance(vec4 clip)
{
    return overlay.w > 0.5 ? clip.z : clip.z + clip.w;
}

void main()
{
    bool atEnd = corner.x > 0.5;
    vec4 color = atEnd ? endColor : startColor;
    linearColor = vec4(toLinear(color.rgb), color.a);
    bias = overlayBias.xy;
    vec4 a = wideViewProjection * vec4(start + overlay.xyz, 1.0);
    vec4 b = wideViewProjection * vec4(end + overlay.xyz, 1.0);
    vec4 bodyA = viewProjection * vec4(start + overlay.xyz, 1.0);
    vec4 bodyB = viewProjection * vec4(end + overlay.xyz, 1.0);
    float nearA = nearDistance(a);
    float nearB = nearDistance(b);
    if (nearA < 0.0 && nearB < 0.0) {
        gl_Position = outside;
        depthAndW = vec2(0.0, 1.0);
        return;
    }
    if (nearA < 0.0) {
        float t = nearA / (nearA - nearB);
        a = mix(a, b, t);
        bodyA = mix(bodyA, bodyB, t);
    } else if (nearB < 0.0) {
        float t = nearB / (nearB - nearA);
        b = mix(b, a, t);
        bodyB = mix(bodyB, bodyA, t);
    }
    vec2 size = target.xy;
    vec2 d = (b.xy / b.w - a.xy / a.w) * size * 0.5;
    float len = length(d);
    d = len > shortest ? d / len : vec2(1.0, 0.0);
    float along = atEnd ? 1.0 : -1.0;
    float half_ = width * overlayScale.x * 0.5;
    vec2 offset = (vec2(-d.y, d.x) * corner.y + d * along) * half_;
    vec4 result = atEnd ? b : a;
    vec4 body = atEnd ? bodyB : bodyA;
    result.xy += offset / (size * 0.5) * result.w;
    gl_Position = result;
    if (overlayPlane.w > 0.5) {
        float onPlane = dot(overlayPlane.xy, result.xy / result.w) + overlayPlane.z;
        depthAndW = vec2(onPlane * result.w, result.w);
    } else {
        depthAndW = vec2(body.z, body.w);
    }
}
