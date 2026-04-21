#version 430 core
// volume.vert — Passagem de quad de tela cheia para volume por raymarching.

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;

out vec2 v_uv;
out vec3 v_ray_dir;

uniform mat4 u_inv_view_proj;

void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;

    // Reconstruir direção do raio no espaço do mundo
    vec4 clip = vec4(a_pos, 1.0, 1.0);
    vec4 world = u_inv_view_proj * clip;
    v_ray_dir = world.xyz / world.w;
}
