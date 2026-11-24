// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) out uint pickedId;
layout(location = 1) out float pickedDepth;

void main()
{
    pickedId = 0u;
    pickedDepth = 1.0;
    gl_FragDepth = 1.0;
}
