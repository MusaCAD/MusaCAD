#version 450 core

// A placed raster image: two textured triangles in world space (IMAGE entities).
layout(location = 0) in vec2 i_position;
layout(location = 1) in vec2 i_uv;

uniform mat3 u_transform; // world -> NDC

out vec2 v_uv;

void main() {
    vec3 ndc = u_transform * vec3(i_position, 1.0);
    gl_Position = vec4(ndc.xy, 0.0, 1.0);
    v_uv = i_uv;
}
