#version 450 core
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
uniform vec4 u_color;
in float v_coverage;
out vec4 frag_color;
void main() {
    frag_color = vec4(u_color.rgb, u_color.a * v_coverage);
}
