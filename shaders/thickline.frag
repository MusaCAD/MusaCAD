#version 450 core

// Round-capped capsule: keep only fragments within the half-width of the segment
// [v_pa, v_pb]. This gives every segment rounded ends, so consecutive segments of
// a tessellated curve overlap at shared vertices with no gap or notch on the outer
// bend -- proper round joins for free, no extra geometry or draw calls (Ph16 B).
flat in vec2 v_pa;
flat in vec2 v_pb;
flat in float v_half;
in vec2 v_px;

uniform vec4 u_color;
out vec4 frag_color;

void main() {
    vec2 pa = v_pa;
    vec2 ba = v_pb - v_pa;
    vec2 pq = v_px - v_pa;
    float denom = max(dot(ba, ba), 1e-12);
    float t = clamp(dot(pq, ba) / denom, 0.0, 1.0); // nearest point param on segment
    float dist = length(pq - ba * t);               // distance to the segment
    if (dist > v_half) {
        discard; // outside the capsule -> rounded cap/join edge
    }
    frag_color = u_color;
}
