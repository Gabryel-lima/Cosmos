#version 430 core
// bloom_threshold.frag — Extrair regiões brilhantes para o bloom.

in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_hdr_tex;
uniform float     u_threshold;  // padrão: 1.0

void main() {
    vec3 hdr = texture(u_hdr_tex, v_uv).rgb;
    // Limiar baseado em luminância
    float lum = dot(hdr, vec3(0.2126, 0.7152, 0.0722));
    float knee = 0.5;
    float soft = clamp(lum - u_threshold + knee, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 1e-5);
    float contrib = max(soft, lum - u_threshold) / max(lum, 1e-5);
    frag_color = vec4(hdr * contrib, 1.0);
}
