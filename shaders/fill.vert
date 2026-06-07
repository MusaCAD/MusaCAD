#version 450 core

// Flat-shaded filled triangles in world space (arrowheads, future hatching).
// One position per vertex (GL_TRIANGLES); no instancing.
layout(location = 0) in vec2 i_position;

uniform mat3 u_transform; // world -> NDC

void main() {
    vec3 ndc = u_transform * vec3(i_position, 1.0);
    gl_Position = vec4(ndc.xy, 0.0, 1.0);
}
