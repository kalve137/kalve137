// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec2 local;
layout(location = 1) flat in vec4 fill;
layout(location = 2) flat in vec4 edge;
layout(location = 3) flat in vec4 frame;
layout(location = 4) flat in vec4 panel;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D atlas;

float roundedBox(vec2 p, vec2 half_, float radius)
{
    vec2 q = abs(p) - half_ + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main()
{
    if (panel.x > 0.5) {
        float cover = texelFetch(atlas, ivec2(frame.zw + floor(local)), 0).a;
        if (cover <= 0.0) {
            discard;
        }
        fragColor = vec4(fill.rgb, fill.a * cover);
        return;
    }
    vec2 size = frame.xy;
    float stroke = panel.z;
    vec2 half_ = size * 0.5 - stroke * 0.5;
    float radius = min(panel.y, min(half_.x, half_.y));
    float d = roundedBox(local - size * 0.5, half_, radius);
    float inside = clamp(0.5 - d, 0.0, 1.0) * fill.a;
    float line = clamp(0.5 + stroke * 0.5 - abs(d), 0.0, 1.0) * edge.a;
    float alpha = line + inside * (1.0 - line);
    if (alpha <= 0.0) {
        discard;
    }
    vec3 rgb = (edge.rgb * line + fill.rgb * inside * (1.0 - line)) / alpha;
    fragColor = vec4(rgb, alpha);
}
