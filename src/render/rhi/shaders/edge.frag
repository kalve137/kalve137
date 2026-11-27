// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) flat in vec3 edgeColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(edgeColor, 1.0);
}
