#version 430 core
// shaders/stromgren.frag — Billboard radial da esfera de Strömgren (Regime 7–8).
// NOVO shader — não modifica nenhum shader existente (Regra 0.6).

flat in float v_layer;

out vec4 frag_color;

void main() {
    vec2  d    = gl_PointCoord - vec2(0.5);
    float dist = length(d) * 2.0;  // 0=centro, 1=borda

    if (dist > 1.0) discard;

    // Gradiente radial: máximo perto do centro, zero na borda
    float fade = 1.0 - smoothstep(0.5, 1.0, dist);

    vec4 color;
    if (v_layer < 0.5) {
        // Inner glow — branco-azulado intenso
        float a = fade * 0.8;
        color = vec4(0.9, 0.95, 1.0, a);
    } else if (v_layer < 1.5) {
        // Mid glow — azul-violeta
        float a = fade * 0.4;
        color = vec4(0.5, 0.3, 1.0, a);
    } else {
        // Outer fade — gradiente para zero
        float a = fade * 0.15;
        color = vec4(0.2, 0.1, 0.6, a);
    }

    if (color.a < 0.005) discard;
    frag_color = color;
}
