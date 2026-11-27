// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) flat in uint id;
layout(location = 1) in vec2 depthAndW;

layout(location = 0) out uint pickedId;
layout(location = 1) out float pickedDepth;

void main()
{
    pickedId = id;
    pickedDepth = depthAndW.x / depthAndW.y;
}
