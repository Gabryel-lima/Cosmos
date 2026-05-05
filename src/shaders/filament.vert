#version 430 core
// shaders/filament.vert — Filamentos da teia cósmica (Regime 8 apenas).
// NOVO shader — não modifica nenhum shader existente (Regra 0.6).
// Renderiza segmentos de linha interpolados entre halos A e B.

layout(location = 0) in vec3  a_pos_a;      // posição halo A
layout(location = 1) in vec3  a_pos_b;      // posição halo B
layout(location = 2) in float a_mass_total; // massa combinada dos dois halos [M☉]
layout(location = 3) in float a_t;          // parâmetro [0,1] ao longo do filamento

uniform mat4  u_mvp;
uniform float u_time;

out float v_alpha;
out vec3  v_color;

void main() {
    vec3 pos    = mix(a_pos_a, a_pos_b, a_t);
    gl_Position = u_mvp * vec4(pos, 1.0);

    // Alpha cai nas extremidades (bordas do filamento são tênues)
    float edge_fade = sin(a_t * 3.14159265);
    float mass_norm = clamp(a_mass_total / 1e12, 0.1, 0.5);
    v_alpha = edge_fade * mass_norm;

    v_color = mix(vec3(0.3, 0.1, 0.5), vec3(0.6, 0.4, 0.9),
                  clamp(a_mass_total / 1e13, 0.0, 1.0));
}
