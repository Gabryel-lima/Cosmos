#version 430 core
// particle.frag — Billboard brilhante com bordas suaves para partículas.

in vec2  v_uv;
in vec4  v_color; // rgb = color, a = shader tag (1.0 = gluon)
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

    bool isGluon = (v_color.a > 0.5);

    vec3 color = v_color.rgb;
    float vivid = 1.0 - smoothstep(0.18, 0.72, alpha);
    float maxc = max(max(color.r, color.g), color.b);
    vec3 hue_preserved = (maxc > 1e-4) ? (color / maxc) * max(maxc, 0.22) : color;
    color = mix(color, hue_preserved * (1.0 + 0.18 * vivid), 0.28 * vivid);

    // Default glow/bright for quarks/others
    vec3 glow = color * alpha;
    vec3 bright = min(color * 3.0, vec3(3.0)) * core;

    // For gluons, draw a distinctive ring + inner core to convey mediator shape
    if (isGluon) {
        // Ring profile: emphasize an annulus around radius ~0.35..0.6
        float ring = smoothstep(0.32, 0.38, d) - smoothstep(0.52, 0.58, d);
        float ring_soft = smoothstep(0.28, 0.38, d) * (1.0 - smoothstep(0.48, 0.58, d));
        float ring_alpha = 0.9 * ring + 0.35 * ring_soft;

        // Add a faint radial streak to hint at directional exchange (visual only)
        float streak = pow(abs(center.x) * 2.0, 6.0) * (1.0 - smoothstep(0.0, 0.9, d));
        vec3 ring_color = mix(color * 1.2, vec3(1.0, 0.9, 0.6), 0.35);

        vec3 ring_glow = ring_color * ring_alpha * 1.6;
        vec3 core_glow = color * (core * 1.4);

        frag_color = vec4((ring_glow + core_glow) * u_opacity, (ring_alpha + alpha * 0.6) * u_opacity);
        return;
    }

    frag_color = vec4((glow + bright) * u_opacity, alpha * 1.0 * u_opacity);
}
