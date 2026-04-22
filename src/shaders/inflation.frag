#version 430 core
// inflation.frag — Visualização do campo escalar de inflação φ(x,y).
// Mapeia valores de φ para um gradiente quente/frio (sobre/subdensidades quânticas).

in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_field_tex;
uniform int       u_mode;       // 0=2D, 1=extrusão 3D
uniform float     u_extrude_t;  // progresso de extrusão 0..1
uniform float     u_opacity;

void main() {
    float phi = texture(u_field_tex, v_uv).r;

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
    col += vec3(0.1) * detail * val;

    // Fade in ao entrar no modo 3D
    float alpha = ((u_mode == 1) ? mix(1.0, 0.0, u_extrude_t) : 1.0) * u_opacity;
    frag_color = vec4(col, alpha);
}
