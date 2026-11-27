// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec4 linearColor;

layout(location = 0) out vec4 fragColor;

vec3 toSrgb(vec3 linear)
{
    vec3 value = clamp(linear, 0.0, 1.0);
    vec3 low = value * 12.92;
    vec3 high = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), value));
}

void main()
{
    fragColor = vec4(toSrgb(linearColor.rgb), linearColor.a);
}
