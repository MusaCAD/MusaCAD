#version 450 core

// The scene's points with their draw order: one position and depth per instance (0 on top
// .. 1 underneath, DRAWORDER).
layout(location = 0) in vec3 i_position;

uniform mat3 u_transform;  // world->NDC
uniform float u_point_size;

void main() {
    vec3 ndc = u_transform * vec3(i_position.xy, 1.0);
    gl_Position = vec4(ndc.xy, i_position.z * 2.0 - 1.0, 1.0);
    gl_PointSize = u_point_size;
}
