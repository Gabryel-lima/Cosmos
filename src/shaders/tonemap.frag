#version 430 core
// tonemap.frag — Mapeamento de tons ACES cinematográfico + vinheta + overlay de flash CMB.

in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_hdr_tex;
uniform float     u_exposure;    // padrão 1.0
uniform float     u_cmb_flash;   // 0..1 overlay aditivo branco brilhante
uniform float     u_blend_t;     // 0..1 crossfade entre regimes

// Aproximação cinematográfica ACES por Krzysztof Narkowicz
vec3 aces_tonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(u_hdr_tex, v_uv).rgb * u_exposure;

    // Adicionar flash do CMB (pulso branco brilhante)
    hdr += vec3(u_cmb_flash * 3.0);

    // Mapeamento de tons ACES
    vec3 ldr = aces_tonemap(hdr);

    // Codificação gamma (assumir framebuffer linear)
    ldr = pow(max(ldr, 0.0), vec3(1.0 / 2.2));

    // Vinheta
    vec2 center = v_uv - 0.5;
    float vig = 1.0 - dot(center, center) * 0.8;
    ldr *= max(vig, 0.0);

    // Granulação sutil de filme
    float grain = fract(sin(dot(v_uv + u_exposure, vec2(12.9898, 78.233))) * 43758.5453);
    ldr += (grain - 0.5) * 0.012;

    frag_color = vec4(clamp(ldr, 0.0, 1.0), 1.0);
}
