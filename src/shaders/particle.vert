#version 430 core
// particle.vert — Partículas billboard instanciadas; posições vindas do SSBO.

layout(location = 0) in vec2 a_corner;  // canto local do quad (-0.5..0.5)

// SSBO 0: float4 por partícula: (camera_rel_x, camera_rel_y, camera_rel_z, size)
layout(std430, binding = 0) buffer ParticlePositions {
    vec4 particle_pos[];
};

// SSBO 1: float4 por partícula: (r, g, b, a)
layout(std430, binding = 1) buffer ParticleColors {
    vec4 particle_col[];
};

uniform mat4 u_view;
uniform mat4 u_proj;

out vec2  v_uv;
out vec4  v_color;
out float v_size;

void main() {
    vec4 pdata = particle_pos[gl_InstanceID];
    vec3 cam_rel_pos = pdata.xyz;
    float size = pdata.w;

    // Billboard: usar vetores direita e cima no espaço da câmera
    vec3 cam_right = vec3(u_view[0][0], u_view[1][0], u_view[2][0]);
    vec3 cam_up    = vec3(u_view[0][1], u_view[1][1], u_view[2][1]);

    vec3 world_pos = cam_rel_pos
                   + cam_right * (a_corner.x * size)
                   + cam_up    * (a_corner.y * size);

    gl_Position = u_proj * u_view * vec4(world_pos, 1.0);

    v_uv    = a_corner + 0.5;
    v_color = particle_col[gl_InstanceID];
    v_size  = size;
}
