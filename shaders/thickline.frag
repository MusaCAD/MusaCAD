#version 450 core

uniform vec4 u_color;
out vec4 frag_color;

void main() {
    frag_color = u_color;
}
