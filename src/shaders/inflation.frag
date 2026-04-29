#version 430 core
// inflation.frag — Visualização do campo escalar de inflação φ(x,y).
// Mapeia valores de φ para um gradiente quente/frio (sobre/subdensidades quânticas).

in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_field_tex;
uniform int       u_mode;       // 0=2D, 1=extrusão 3D
uniform float     u_extrude_t;  // progresso de extrusão 0..1
uniform float     u_opacity;
uniform float     u_gradient_boost;
uniform float     u_interference_strength;

void main() {
    float phi = texture(u_field_tex, v_uv).r;
    vec2 texel = 1.0 / vec2(textureSize(u_field_tex, 0));
    float phi_x1 = texture(u_field_tex, clamp(v_uv + vec2(texel.x, 0.0), 0.0, 1.0)).r;
    float phi_x0 = texture(u_field_tex, clamp(v_uv - vec2(texel.x, 0.0), 0.0, 1.0)).r;
    float phi_y1 = texture(u_field_tex, clamp(v_uv + vec2(0.0, texel.y), 0.0, 1.0)).r;
    float phi_y0 = texture(u_field_tex, clamp(v_uv - vec2(0.0, texel.y), 0.0, 1.0)).r;
    float gradient = length(vec2(phi_x1 - phi_x0, phi_y1 - phi_y0)) * u_gradient_boost;

    // Amplificar flutuações e mapear de forma mais rica e "plasmática"
    // Ao invés de um clamp seco, criamos um gradiente contínuo e vibrante.
    float val = smoothstep(-0.05, 0.05, phi);

    // Paleta de plasma denso: azul marinho profundo -> roxo elétrico -> laranja vivo -> branco
    vec3 col_cold = vec3(0.0, 0.05, 0.3);
    vec3 col_mid  = vec3(0.4, 0.0, 0.6);
    vec3 col_hot  = vec3(1.0, 0.4, 0.0);
    vec3 col_core = vec3(1.0, 1.0, 0.9);

    vec3 col;
    if (val < 0.5) {
        col = mix(col_cold, col_mid, val * 2.0);
    } else {
        float f = (val - 0.5) * 2.0;
        col = mix(col_mid, col_hot, f);
        // Core brilhante apenas nos picos mais elevados
        col = mix(col, col_core, smoothstep(0.7, 1.0, f));
    }

    // Módulação adicional para textura "borbulhante" usando ruído de alta frequência da própria textura
    float detail = texture(u_field_tex, v_uv * 4.0).r;
    float interference = sin((phi + detail * 0.5) * 55.0) * 0.5 + 0.5;
    col += vec3(0.1) * detail * val;
    col += vec3(0.18, 0.12, 0.08) * gradient;
    col = mix(col, col.bgr * vec3(0.9, 0.8, 1.1), interference * u_interference_strength);

    // Fade in ao entrar no modo 3D
    float alpha = ((u_mode == 1) ? mix(1.0, 0.0, u_extrude_t) : 1.0) * u_opacity;
    frag_color = vec4(col, alpha);
}
