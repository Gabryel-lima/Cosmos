#version 430 core
// shaders/star_formation.frag — Efeito de colapso de formação estelar (Regime 7–8).
// NOVO shader — não modifica nenhum shader existente (Regra 0.6).

flat in float v_phase;
in     float v_glow_t;

out vec4 frag_color;

void main() {
    vec2  d    = gl_PointCoord - vec2(0.5);
    float dist = length(d) * 2.0;
    if (dist > 1.0) discard;

    float fade = 1.0 - smoothstep(0.3, 1.0, dist);

    vec4 color;
    if (v_phase < 0.5) {
        // COLLAPSING — partículas convergindo: laranja avermelhado difuso
        float a = fade * v_glow_t * 0.5;
        color = vec4(1.0, 0.5, 0.15, a);
    } else if (v_phase < 1.5) {
        // PROTO_STAR — núcleo branco-azulado crescendo
        float core = exp(-dist*dist / 0.04);
        float a    = (fade * 0.4 + core * 0.6) * v_glow_t;
        color = vec4(0.85, 0.90, 1.0, a);
    } else {
        // STAR_BORN — brilho total estabelecido
        float core = exp(-dist*dist / 0.005);
        float halo = exp(-dist*dist / 0.1) * 0.3;
        float a    = clamp(core + halo, 0.0, 1.0) * 0.9;
        color = vec4(1.0, 0.97, 0.88, a);
    }

    if (color.a < 0.005) discard;
    frag_color = color;
}
