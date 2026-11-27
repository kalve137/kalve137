// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec2 corner;
layout(location = 1) in vec3 anchor;
layout(location = 2) in vec4 color;
layout(location = 3) in vec4 pick;
layout(location = 4) in vec3 sizes;
layout(location = 5) in vec4 style;

layout(location = 0) out vec4 linearColor;
layout(location = 1) out vec2 local;
layout(location = 2) flat out vec4 shape;

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

const vec4 outside = vec4(2.0, 2.0, 2.0, 1.0);

vec3 toLinear(vec3 encoded)
{
    vec3 low = encoded / 12.92;
    vec3 high = pow((encoded + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), encoded));
}

void main()
{
    float ratio = overlayScale.x;
    float ring = sizes.x * ratio;
    float cross = sizes.y * ratio;
    float dot_ = sizes.z * ratio;
    float kind = floor(style.x * 255.0 + 0.5);
    float flags = floor(style.y * 255.0 + 0.5);
    linearColor = vec4(toLinear(color.rgb), color.a);
    shape = vec4(kind, ring, cross, dot_);
    vec4 clip = viewProjection * vec4(anchor, 1.0);
    if (clip.w <= 0.0) {
        gl_Position = outside;
        local = vec2(0.0);
        return;
    }
    vec2 size = target.xy;
    vec2 pixel = (clip.xy / clip.w * 0.5 + 0.5) * size;
    vec2 center = floor(pixel) + 0.5;
    float reach = floor(max(ring, cross) * 0.5) + 1.0;
    vec2 offset = vec2(corner.x * 2.0 - 1.0, corner.y) * reach;
    local = offset;
    vec2 ndc = (center + offset) / size * 2.0 - 1.0;
    bool topmost = mod(flags, 2.0) > 0.5;
    float z = topmost ? (overlay.w > 0.5 ? 0.5 : 0.0) : clip.z / clip.w;
    gl_Position = vec4(ndc, z, 1.0);
}
