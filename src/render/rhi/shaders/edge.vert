// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec2 corner;
layout(location = 1) in vec3 start;
layout(location = 2) in vec3 end;
layout(location = 3) in vec4 entryBytes;

layout(location = 0) flat out vec3 edgeColor;

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

layout(binding = 1) uniform sampler2D palette;

const int normalState = 0;
const int hoveredState = 1;
const int selectedState = 2;
const int selectedHoveredState = 3;

const float nearW = 1e-6;
const float shortest = 1e-9;
const vec4 outside = vec4(2.0, 2.0, 2.0, 1.0);

vec4 expandEdge(vec4 a, vec4 b, vec2 at, float widthPixels, vec2 size)
{
    if (a.w < nearW && b.w < nearW) {
        return outside;
    }
    if (a.w < nearW) {
        a = mix(a, b, (nearW - a.w) / (b.w - a.w));
    } else if (b.w < nearW) {
        b = mix(b, a, (nearW - b.w) / (a.w - b.w));
    }
    vec2 d = (b.xy / b.w - a.xy / a.w) * size * 0.5;
    float len = length(d);
    d = len > shortest ? d / len : vec2(1.0, 0.0);
    bool atEnd = at.x > 0.5;
    float along = atEnd ? 1.0 : -1.0;
    float half_ = widthPixels * 0.5;
    vec2 offset = (vec2(-d.y, d.x) * at.y + d * along) * half_;
    vec4 result = atEnd ? b : a;
    result.xy += offset / (size * 0.5) * result.w;
    return result;
}

vec3 pushAway(vec3 color, float amount)
{
    float toward = dot(color, vec3(0.2126, 0.7152, 0.0722)) >= steps.y ? 0.0 : 1.0;
    return mix(color, vec3(toward), amount);
}

void main()
{
    int entry = int(round(entryBytes.x * 255.0)) + int(round(entryBytes.y * 255.0)) * 256;
    vec4 value = texelFetch(palette, ivec2(entry % 256, entry / 256), 0);
    int state = int(round(value.r * 255.0));
    bool selected = state == selectedState || state == selectedHoveredState;
    edgeColor = selected ? gold.rgb : edge.rgb;
    if (state == hoveredState || state == selectedHoveredState) {
        edgeColor = pushAway(edgeColor, steps.x);
    }
    bool hiddenByStyle = options.y > 0.5 && state == normalState;
    if ((int(round(value.a * 255.0)) & 1) == 0 || hiddenByStyle) {
        gl_Position = outside;
        return;
    }
    float width = state == normalState ? target.z : target.w;
    gl_Position = expandEdge(viewProjection * vec4(start, 1.0), viewProjection * vec4(end, 1.0), corner, width,
                             target.xy);
}
