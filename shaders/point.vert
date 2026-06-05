#version 450 core

// Instanced point rendering. Each instance is one point position (x, y); one
// base vertex per instance.
layout(location = 0) in vec2 i_position;

uniform mat3 u_transform;  // world->NDC
uniform float u_point_size;

void main() {
    vec3 ndc = u_transform * vec3(i_position, 1.0);
    gl_Position = vec4(ndc.xy, 0.0, 1.0);
    gl_PointSize = u_point_size;
}
