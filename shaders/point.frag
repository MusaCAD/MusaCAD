#version 450 core

uniform vec4 u_color;
out vec4 frag_color;

void main() {
    // Round points: discard fragments outside the unit circle of gl_PointCoord.
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    if (dot(d, d) > 1.0) {
        discard;
    }
    frag_color = u_color;
}
