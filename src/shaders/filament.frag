#version 430 core
// shaders/filament.frag — Filamentos da teia cósmica (Regime 8 apenas).
// NOVO shader — não modifica nenhum shader existente (Regra 0.6).

in float v_alpha;
in vec3  v_color;

out vec4 frag_color;

void main() {
    if (v_alpha < 0.005) discard;
    frag_color = vec4(v_color, v_alpha);
}
