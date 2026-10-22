#version 450 core

// The scene's thick lines with their draw order: thickline.vert, each instance carrying the
// segment's depth too (0 on top .. 1 underneath, DRAWORDER), so a fill drawn over a line
// hides it and one drawn under it does not.
layout(location = 0) in vec4 i_segment;
layout(location = 1) in float i_depth;

uniform mat3 u_transform;   // world -> NDC
uniform vec2 u_viewport;    // framebuffer size in pixels
uniform float u_halfwidth;  // half line width in pixels

flat out vec2 v_pa;         // segment start (px)
flat out vec2 v_pb;         // segment end   (px)
flat out float v_half;      // half width    (px)
out vec2 v_px;              // this vertex's position (px), interpolated

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

    vec2 base = (gl_VertexID < 2) ? (pa - dir * u_halfwidth) : (pb + dir * u_halfwidth);
    vec2 off = ((gl_VertexID & 1) == 0) ? perp : -perp;
    vec2 px = base + off;

    v_pa = pa;
    v_pb = pb;
    v_half = u_halfwidth;
    v_px = px;

    vec2 ndc = (px / u_viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc, i_depth * 2.0 - 1.0, 1.0);
}
