#version 430 core
// shaders/gas_splat.vert — Gaussian splat para gás (Regimes 6–8).
// NOVO shader — não modifica nenhum shader existente (Regra 0.6).

layout(location = 0) in vec3  a_position;      // posição da partícula
layout(location = 1) in float a_smoothing;     // smoothing length SPH
layout(location = 2) in float a_temperature;   // temperatura local (keV)
layout(location = 3) in int   a_ionized;       // 0=neutro, 1=ionizado

uniform mat4  u_view;
uniform mat4  u_proj;
uniform float u_sigma_px;      // raio gaussiano mínimo em pixels (do QualityConfig)
uniform vec2  u_screen_size;

out float v_sigma;
out float v_temperature;
flat out int  v_ionized;

void main() {
    vec4 view_pos  = u_view * vec4(a_position, 1.0);
    gl_Position    = u_proj * view_pos;

    // Projetar smoothing_length para pixels na tela
    float dist     = max(-view_pos.z, 0.001);
    float proj_scale = u_proj[1][1];  // cot(fov/2) — fator de escala vertical
    float proj_sigma = a_smoothing * proj_scale * u_screen_size.y / (2.0 * dist);
    v_sigma = max(u_sigma_px, proj_sigma);

    // Tamanho do ponto para cobrir 3× o raio do splat
    gl_PointSize = clamp(v_sigma * 3.0, 1.0, 256.0);

    v_temperature = a_temperature;
    v_ionized     = a_ionized;
}
