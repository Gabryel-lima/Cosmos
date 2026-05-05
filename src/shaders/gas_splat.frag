#version 430 core
// shaders/gas_splat.frag — Gaussian splat para gás (Regimes 6–8).
// NOVO shader — não modifica nenhum shader existente (Regra 0.6).

in  float v_sigma;
in  float v_temperature;
flat in int  v_ionized;

out vec4 frag_color;

// Mapear temperatura para cor — gás neutro vs ionizado
vec3 TemperatureToColor(float temp_kev, int ionized) {
    if (ionized == 1) {
        // Gás ionizado: azul-violeta quente
        return mix(vec3(0.4, 0.2, 0.9), vec3(0.8, 0.6, 1.0),
                   clamp(temp_kev * 1000.0, 0.0, 1.0));
    } else {
        // Gás neutro: azul-acinzentado frio
        return mix(vec3(0.05, 0.05, 0.12), vec3(0.15, 0.2, 0.4),
                   clamp(temp_kev * 5000.0, 0.0, 1.0));
    }
}

void main() {
    // gl_PointCoord: [0,1]×[0,1], centrado em (0.5, 0.5)
    vec2  d      = gl_PointCoord - vec2(0.5);
    float dist2  = dot(d, d);
    // Variância normalizada para coord de ponto sprite: (0.2)^2 = 0.04
    float sigma2 = 0.04;

    float alpha = exp(-dist2 / (2.0 * sigma2));

    // Descarte antecipado — não desperdiçar fillrate
    if (alpha < 0.01) discard;

    vec3 color = TemperatureToColor(v_temperature, v_ionized);
    frag_color  = vec4(color * alpha, alpha * 0.4);
}
