// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) flat in uint id;
layout(location = 1) in vec2 depthAndW;
layout(location = 2) flat in vec4 mode;

layout(location = 0) out uint pickedId;
layout(location = 1) out float pickedDepth;

void main()
{
    float depth = depthAndW.x / depthAndW.y;
    if (mode.x < 0.5) {
        depth = depth * 0.5 + 0.5;
    }
    float slope = max(abs(dFdx(depth)), abs(dFdy(depth)));
    pickedId = id;
    pickedDepth = depth;
    gl_FragDepth = mode.y > 0.5 ? gl_FragCoord.z : clamp(depth + slope * mode.z + mode.w, 0.0, 1.0);
}
