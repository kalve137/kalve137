// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec3 linearColor;
layout(location = 1) in vec3 surfaceNormal;
layout(location = 2) in vec3 surfacePosition;

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
    vec3 color = linearColor;
    if (options.x > 0.5) {
        vec3 view = eyePosition.w > 0.5 ? normalize(eyePosition.xyz - surfacePosition) : eye.xyz;
        vec3 side = normalize(surfaceNormal);
        if (dot(side, view) < 0.0) {
            side = -side;
        }
        float diffuse = max(0.0, dot(side, light.xyz));
        float shine = 0.0;
        if (diffuse > 0.0) {
            shine = pow(max(0.0, dot(side, normalize(light.xyz + view))), look.z);
        }
        color = linearColor * (look.x + look.y * diffuse) + specular.rgb * shine;
    }
    fragColor = vec4(toSrgb(color), 1.0);
}
