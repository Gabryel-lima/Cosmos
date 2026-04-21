#version 430 core
// particle.frag — Billboard brilhante com bordas suaves para partículas.

in vec2  v_uv;
in vec4  v_color;
in float v_size;

out vec4 frag_color;

uniform float u_opacity;

void main() {
    // Queda radial suave centralizada em (0.5, 0.5)
    vec2 center = v_uv - 0.5;
    float d = length(center) * 2.0;   // 0..1 do centro à borda
    if (d > 1.0) discard;

    // Perfil gaussiano com cauda de brilho
    float alpha = exp(-3.5 * d * d);

    // Núcleo interno brilhante
    float core = exp(-12.0 * d * d);

    vec3 glow = v_color.rgb * alpha;
    vec3 bright = min(v_color.rgb * 3.0, vec3(3.0)) * core;

    frag_color = vec4((glow + bright) * u_opacity, alpha * v_color.a * u_opacity);
}
