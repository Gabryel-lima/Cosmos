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
    vec3 world_rel_pos = pdata.xyz;
    float size = pdata.w;

    // Particle centres arrive in world space relative to the camera origin.
    // Rotate them into camera space once, then build the billboard in camera axes.
    vec3 cam_space_center = mat3(u_view) * world_rel_pos;
    vec3 cam_space_vertex = cam_space_center + vec3(a_corner * size, 0.0);

    gl_Position = u_proj * vec4(cam_space_vertex, 1.0);

    v_uv    = a_corner + 0.5;
    v_color = particle_col[gl_InstanceID];
    v_size  = size;
}
