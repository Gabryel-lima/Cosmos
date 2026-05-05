#version 430 core
// shaders/star_glow.vert — Billboard de brilho estelar (Regimes 7–8).
// NOVO shader — não modifica nenhum shader existente (Regra 0.6).

layout(location = 0) in vec3  a_position;    // posição da estrela
layout(location = 1) in float a_luminosity;  // luminosidade [0..1+]
layout(location = 2) in int   a_star_state;  // StarState enum (0=NONE, 1=PROTO, 2=MAIN...)
layout(location = 3) in float a_temperature; // temperatura [K]

uniform mat4  u_view;
uniform mat4  u_proj;
uniform float u_base_size;   // tamanho base em pixels, do QualityConfig
uniform vec2  u_screen_size;

out float v_luminosity;
out float v_temperature;
flat out int   v_star_state;

void main() {
    vec4 view_pos  = u_view * vec4(a_position, 1.0);
    gl_Position    = u_proj * view_pos;

    // Escalar tamanho pela luminosidade — limitar para evitar overdraw
    float size = u_base_size * (1.0 + sqrt(max(a_luminosity, 0.0)) * 2.0);
    gl_PointSize = clamp(size, 2.0, 128.0);

    v_luminosity  = a_luminosity;
    v_temperature = a_temperature;
    v_star_state  = a_star_state;
}
