#version 430 core
// shaders/stromgren.vert — Billboard radial da esfera de Strömgren (Regime 7–8).
// NOVO shader — não modifica nenhum shader existente (Regra 0.6).

layout(location = 0) in vec3  a_position;   // posição da estrela ionizante
layout(location = 1) in float a_radius;     // raio de Strömgren visual [sim units]
layout(location = 2) in float a_layer;      // 0=inner, 1=mid, 2=outer

uniform mat4  u_view;
uniform mat4  u_proj;
uniform vec2  u_screen_size;

out float v_radius;
flat out float v_layer;

void main() {
    vec4 view_pos = u_view * vec4(a_position, 1.0);
    gl_Position   = u_proj * view_pos;

    float dist     = max(-view_pos.z, 0.001);
    float proj_fac = u_proj[1][1];
    // Projetar raio para pixels
    float r_px = a_radius * proj_fac * u_screen_size.y / (2.0 * dist);
    gl_PointSize = clamp(r_px * 2.5, 4.0, 512.0);

    v_radius = a_radius;
    v_layer  = a_layer;
}
