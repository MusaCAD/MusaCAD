#version 410 core
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
// UI font coverage is rasterized and cached on the UI thread at device resolution.
layout(location = 0) in vec2 i_position;
layout(location = 1) in float i_coverage;
uniform mat3 u_transform;
out float v_coverage;
void main() {
    vec3 ndc = u_transform * vec3(i_position, 1.0);
    gl_Position = vec4(ndc.xy, 0.0, 1.0);
    v_coverage = i_coverage;
}
