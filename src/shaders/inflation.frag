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

    // Amplificar flutuações (σ ~ 0.01) para visualização
    float val = clamp(phi * 50.0, -1.0, 1.0);

    vec3 col;
    if (val > 0.0) {
        // Sobredensidade: preto → laranja → branco
        col = mix(vec3(0.0, 0.0, 0.02), vec3(1.0, 0.5, 0.0), val);
        col = mix(col, vec3(1.0, 1.0, 0.9), val * val);
    } else {
        // Subdensidade: preto → azul → ciano
        col = mix(vec3(0.0, 0.0, 0.02), vec3(0.0, 0.3, 1.0), -val);
        col = mix(col, vec3(0.0, 0.9, 1.0), val * val);
    }

    // Fade in ao entrar no modo 3D
    float alpha = ((u_mode == 1) ? mix(1.0, 0.0, u_extrude_t) : 1.0) * u_opacity;
    frag_color = vec4(col, alpha);
}
