// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec3 position;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
};

void main()
{
    gl_Position = viewProjection * vec4(position, 1.0);
}
