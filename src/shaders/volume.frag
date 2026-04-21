#version 430 core
// volume.frag — Raymarching frente-para-trás pelo campo de densidade 3D.

in vec2 v_uv;
in vec3 v_ray_dir;

out vec4 frag_color;

uniform sampler3D u_density_tex;
uniform float     u_density_scale;
uniform float     u_opacity_scale;
uniform float     u_opacity;

#define MAX_STEPS 128
#define STEP_SIZE 0.016     // in [0,1]^3 volume coords

// Função de transferência: densidade → (cor, alpha)
vec4 transferFunction(float density) {
    // Baixa densidade: azul/roxo; média: laranja; alta: branco
    vec3 cool  = vec3(0.1, 0.15, 0.6);
    vec3 warm  = vec3(1.0, 0.5,  0.1);
    vec3 hot   = vec3(1.0, 1.0,  1.0);

    float t = clamp(density * u_density_scale, 0.0, 1.0);
    vec3 col = mix(cool, warm, smoothstep(0.0, 0.5, t));
    col = mix(col, hot, smoothstep(0.5, 1.0, t));
        float alpha = clamp(density * u_opacity_scale, 0.0, 1.0) * u_opacity;
    return vec4(col, alpha);
}

void main() {
    // Iniciar raio no lado próximo do volume [0,1]^3
    vec3 ray_origin = vec3(0.5);  // centro do volume
    vec3 ray_dir = normalize(v_ray_dir);

    // Interseção simples com caixa alinhada aos eixos
    vec3 inv_dir = 1.0 / (ray_dir + 1e-8);
    vec3 t0 = (vec3(0.0) - ray_origin) * inv_dir;
    vec3 t1 = (vec3(1.0) - ray_origin) * inv_dir;
    vec3 tmin3 = min(t0, t1);
    vec3 tmax3 = max(t0, t1);
    float t_enter = max(max(tmin3.x, tmin3.y), tmin3.z);
    float t_exit  = min(min(tmax3.x, tmax3.y), tmax3.z);

    if (t_enter > t_exit) discard;

    t_enter = max(t_enter, 0.0);

    vec4 accum = vec4(0.0);
    float t = t_enter;
    for (int i = 0; i < MAX_STEPS; ++i) {
        if (t > t_exit || accum.a > 0.99) break;
        vec3 pos = ray_origin + ray_dir * t;
        float density = texture(u_density_tex, pos).r;
        vec4 sample_col = transferFunction(density);

        // Composição frente-para-trás
        accum.rgb += (1.0 - accum.a) * sample_col.a * sample_col.rgb;
        accum.a   += (1.0 - accum.a) * sample_col.a;
        t += STEP_SIZE;
    }

    frag_color = accum;
}
