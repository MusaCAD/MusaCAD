#version 450 core

// Instanced line rendering. Each instance is one segment: (ax, ay, bx, by).
// Two base vertices per instance; gl_VertexID selects the endpoint. No
// per-vertex buffer is needed -- positions come entirely from per-instance data.
layout(location = 0) in vec4 i_segment;

uniform mat3 u_transform; // world->NDC (or screen->NDC for the overlay)

void main() {
    vec2 p = (gl_VertexID == 0) ? i_segment.xy : i_segment.zw;
    vec3 ndc = u_transform * vec3(p, 1.0);
    gl_Position = vec4(ndc.xy, 0.0, 1.0);
}
