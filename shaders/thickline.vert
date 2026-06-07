#version 450 core

// Thick-line rendering: each instance is one segment (ax, ay, bx, by) drawn as a
// screen-space-expanded quad (triangle strip, 4 base vertices). Width is in
// pixels and therefore zoom-independent, matching AutoCAD's default lineweight
// display. Square caps (each end extended by the half-width) overlap at polyline
// vertices so joins show no gaps -- a cheap stand-in for true miters.
layout(location = 0) in vec4 i_segment;

uniform mat3 u_transform;   // world -> NDC
uniform vec2 u_viewport;    // framebuffer size in pixels
uniform float u_halfwidth;  // half line width in pixels

vec2 to_px(vec2 world) {
    vec3 ndc = u_transform * vec3(world, 1.0);
    return (ndc.xy * 0.5 + 0.5) * u_viewport;
}

void main() {
    vec2 pa = to_px(i_segment.xy);
    vec2 pb = to_px(i_segment.zw);
    vec2 d = pb - pa;
    float len = length(d);
    vec2 dir = len > 1e-6 ? d / len : vec2(1.0, 0.0);
    vec2 perp = vec2(-dir.y, dir.x) * u_halfwidth;

    // gl_VertexID 0,1 at the a-end (extended back), 2,3 at the b-end (extended).
    vec2 base = (gl_VertexID < 2) ? (pa - dir * u_halfwidth) : (pb + dir * u_halfwidth);
    vec2 off = ((gl_VertexID & 1) == 0) ? perp : -perp;
    vec2 px = base + off;

    vec2 ndc = (px / u_viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
