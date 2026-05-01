#version 430 core
// tonemap.frag — Mapeamento de tons ACES cinematográfico + vinheta + overlay de flash CMB.

in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_hdr_tex;
uniform float     u_exposure;    // padrão 1.0
uniform float     u_cmb_flash;   // 0..1 overlay aditivo branco brilhante
uniform float     u_blend_t;     // 0..1 crossfade entre regimes
uniform float     u_saturation;
uniform float     u_contrast;
uniform float     u_vignette_strength;
uniform float     u_grain_strength;
uniform vec3      u_shadow_tint;
uniform vec3      u_highlight_tint;

// Aproximação cinematográfica ACES por Krzysztof Narkowicz
vec3 aces_tonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float sanitizeScalar(float value) {
    return (isnan(value) || isinf(value)) ? 0.0 : value;
}

vec3 sanitizeVec3(vec3 value) {
    return vec3(sanitizeScalar(value.x),
                sanitizeScalar(value.y),
                sanitizeScalar(value.z));
}

void main() {
    vec3 hdr = sanitizeVec3(texture(u_hdr_tex, v_uv).rgb) * sanitizeScalar(u_exposure);
    hdr = clamp(hdr, vec3(0.0), vec3(65504.0));

    // Adicionar flash do CMB (pulso branco brilhante)
    hdr += vec3(u_cmb_flash * 3.0);

    // Mapeamento de tons ACES
    vec3 ldr = sanitizeVec3(aces_tonemap(hdr));

    float luma = luminance(ldr);
    float shadow_mix = smoothstep(0.55, 0.0, luma);
    float highlight_mix = smoothstep(0.35, 1.0, luma);
    ldr *= mix(vec3(1.0), u_shadow_tint, shadow_mix);
    ldr *= mix(vec3(1.0), u_highlight_tint, highlight_mix);
    ldr = mix(vec3(luma), ldr, u_saturation);
    ldr = (ldr - 0.5) * u_contrast + 0.5;

    // Codificação gamma (assumir framebuffer linear)
    ldr = pow(max(ldr, 0.0), vec3(1.0 / 2.2));

    // Vinheta
    vec2 center = v_uv - 0.5;
    float vig = 1.0 - dot(center, center) * u_vignette_strength;
    ldr *= max(vig, 0.0);

    // Granulação sutil de filme
    float grain = fract(sin(dot(v_uv + u_exposure, vec2(12.9898, 78.233))) * 43758.5453);
    ldr += (grain - 0.5) * u_grain_strength * mix(1.0, 0.7, u_blend_t);
    ldr = sanitizeVec3(ldr);

    frag_color = vec4(clamp(ldr, 0.0, 1.0), 1.0);
}
