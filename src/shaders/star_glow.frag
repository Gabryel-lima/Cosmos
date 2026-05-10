#version 430 core
// shaders/star_glow.frag — Billboard de brilho estelar (Regimes 7–8).
// NOVO shader — não modifica nenhum shader existente (Regra 0.6).

in  float v_luminosity;
in  float v_temperature;
flat in  int   v_star_state;

out vec4 frag_color;

uniform sampler2D u_profile_tex;

// Temperatura estelar → cor via aproximação de corpo negro
vec3 StarColor(float temp_k) {
    // Mapeamento simplificado: azul (>10000K) → branco (6000K) → amarelo (5000K) → laranja/vermelho (<4000K)
    float t = clamp(temp_k / 10000.0, 0.0, 1.0);
    vec3 cool = vec3(1.0, 0.55, 0.15);  // tipo K/M
    vec3 warm = vec3(1.0, 0.95, 0.82);  // tipo G/F (sol)
    vec3 hot  = vec3(0.75, 0.85, 1.0);  // tipo A/B/O
    if (t < 0.5)
        return mix(cool, warm, t * 2.0);
    else
        return mix(warm, hot, (t - 0.5) * 2.0);
}

void main() {
    vec2 texel = vec2(1.0) / vec2(textureSize(u_profile_tex, 0));
    float total = texture(u_profile_tex, gl_PointCoord).r;
    float core = texture(u_profile_tex,
                         clamp(gl_PointCoord * 0.55 + vec2(0.225), texel, vec2(1.0) - texel)).r;

    if (total < 0.005) discard;

    // Cor estelar + tint de protostar em azul-branco frio
    vec3 color;
    if (v_star_state == 1) {
        // PROTOSTAR — branco-azulado tênue
        color = mix(vec3(0.4, 0.5, 0.8), vec3(0.9, 0.95, 1.0), core);
    } else {
        color = StarColor(v_temperature);
    }

    // Escalar pelo brilho
    float lum_scale = 0.4 + sqrt(max(v_luminosity, 0.0)) * 0.6;
    frag_color = vec4(color * total * lum_scale, total * 0.85);
}
