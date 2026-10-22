#version 450 core

// The scene's filled triangles (hatches, wipeouts, filled text, arrowheads) with their draw
// order: a world position and a depth per vertex (0 on top .. 1 underneath, DRAWORDER).
layout(location = 0) in vec3 i_position;

uniform mat3 u_transform; // world -> NDC

void main() {
    vec3 ndc = u_transform * vec3(i_position.xy, 1.0);
    gl_Position = vec4(ndc.xy, i_position.z * 2.0 - 1.0, 1.0);
}
