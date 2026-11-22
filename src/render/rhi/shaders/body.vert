// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 normalAndEntry;

layout(location = 0) out vec3 linearColor;
layout(location = 1) out vec3 surfaceNormal;
layout(location = 2) out vec3 surfacePosition;
layout(location = 3) flat out vec4 highlight;

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
    vec4 hover;
    vec4 selected;
};

layout(binding = 1) uniform sampler2D palette;

const int visibleFlag = 1;
const int hoveredFlag = 2;
const int selectedFlag = 4;

vec3 toLinear(vec3 encoded)
{
    vec3 low = encoded / 12.92;
    vec3 high = pow((encoded + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), encoded));
}

vec3 decodeNormal(vec2 encoded)
{
    vec2 folded = encoded * 2.0 - 1.0;
    vec3 normal = vec3(folded, 1.0 - abs(folded.x) - abs(folded.y));
    float fold = max(-normal.z, 0.0);
    normal.x += normal.x >= 0.0 ? -fold : fold;
    normal.y += normal.y >= 0.0 ? -fold : fold;
    return normalize(normal);
}

void main()
{
    int entry = int(round(normalAndEntry.z * 255.0)) + int(round(normalAndEntry.w * 255.0)) * 256;
    vec4 value = texelFetch(palette, ivec2(entry % 256, entry / 256), 0);
    int flags = int(round(value.a * 255.0));
    linearColor = toLinear(value.rgb);
    surfaceNormal = decodeNormal(normalAndEntry.xy);
    surfacePosition = position;
    highlight = (flags & selectedFlag) != 0 ? vec4(toLinear(selected.rgb), 1.0)
                : (flags & hoveredFlag) != 0 ? vec4(toLinear(hover.rgb), hover.a)
                                             : vec4(0.0);
    if ((flags & visibleFlag) == 0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    gl_Position = viewProjection * vec4(position, 1.0);
}
