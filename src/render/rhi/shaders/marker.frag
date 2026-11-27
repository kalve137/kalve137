// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec4 linearColor;
layout(location = 1) in vec2 local;
layout(location = 2) flat in vec4 shape;

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

const float sixth = 0.5235988;

vec3 toSrgb(vec3 linear)
{
    vec3 value = clamp(linear, 0.0, 1.0);
    vec3 low = value * 12.92;
    vec3 high = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), value));
}

float disc(float radius, float len)
{
    return clamp(radius + 0.5 - len, 0.0, 1.0);
}

float circle(float diameter, float stroke, float len)
{
    float radius = diameter * 0.5 - stroke * 0.5;
    return clamp(0.5 + stroke * 0.5 - abs(len - radius), 0.0, 1.0);
}

float bar(vec2 p, vec2 along, float span, float stroke)
{
    float a = abs(dot(p, along));
    float b = abs(dot(p, vec2(-along.y, along.x)));
    return clamp(0.5 + stroke * 0.5 - b, 0.0, 1.0) * clamp(span * 0.5 + 0.5 - a, 0.0, 1.0);
}

float square(vec2 p, float side)
{
    float whole = floor(side + 0.5);
    vec2 shift = mod(whole, 2.0) < 0.5 ? vec2(-0.5, 0.5) : vec2(0.0);
    vec2 edge_ = clamp(whole * 0.5 + 0.5 - abs(p - shift), 0.0, 1.0);
    return edge_.x * edge_.y;
}

void main()
{
    float stroke = overlayScale.x;
    float ring = shape.y;
    float cross = shape.z;
    float len = length(local);
    float kind = shape.x;
    float cover = 0.0;
    if (kind < 0.5) {
        cover = disc(ring * 0.5, len);
    } else if (kind < 1.5) {
        cover = max(circle(ring, stroke, len), square(local, shape.w));
    } else if (kind < 2.5) {
        cover = max(bar(local, vec2(1.0, 0.0), cross, stroke), bar(local, vec2(0.0, 1.0), cross, stroke));
    } else if (kind < 3.5) {
        float plus = max(bar(local, vec2(1.0, 0.0), cross, stroke), bar(local, vec2(0.0, 1.0), cross, stroke));
        cover = max(circle(ring, stroke, len), plus);
    } else {
        float diagonal = ring / cos(sixth) - 1.0;
        float star = bar(local, vec2(0.0, 1.0), cross, stroke);
        star = max(star, bar(local, vec2(cos(sixth), sin(sixth)), diagonal, stroke));
        star = max(star, bar(local, vec2(cos(sixth), -sin(sixth)), diagonal, stroke));
        cover = max(circle(ring, stroke, len), star);
    }
    if (cover <= 0.0) {
        discard;
    }
    fragColor = vec4(toSrgb(linearColor.rgb), linearColor.a * cover);
}
