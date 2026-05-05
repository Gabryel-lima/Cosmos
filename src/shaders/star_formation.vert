#version 430 core
// shaders/star_formation.vert — Efeito de colapso de formação estelar (Regime 7–8).
// NOVO shader — não modifica nenhum shader existente (Regra 0.6).

layout(location = 0) in vec3  a_position;    // posição central do colapso
layout(location = 1) in float a_glow_radius; // raio atual do glow [0..final_radius]
layout(location = 2) in float a_phase;       // 0=COLLAPSING, 1=PROTO_STAR, 2=STAR_BORN

uniform mat4 u_view;
uniform mat4 u_proj;
uniform vec2 u_screen_size;

flat out float v_phase;
out     float v_glow_t;   // progresso normalizado do glow [0..1]

void main() {
    vec4 view_pos = u_view * vec4(a_position, 1.0);
    gl_Position   = u_proj * view_pos;

    float dist    = max(-view_pos.z, 0.001);
    float proj_fac = u_proj[1][1];
    float r_px    = a_glow_radius * proj_fac * u_screen_size.y / (2.0 * dist);
    gl_PointSize  = clamp(r_px * 2.5, 3.0, 256.0);

    v_phase  = a_phase;
    v_glow_t = clamp(a_glow_radius, 0.0, 1.0);
}
