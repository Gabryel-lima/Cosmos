#version 430 core
// volume.frag — Raymarching frente-para-trás pelo campo de densidade 3D.

in vec2 v_uv;
in vec3 v_ray_dir;

out vec4 frag_color;

uniform sampler3D u_density_tex;
uniform float     u_density_scale;
uniform float     u_opacity_scale;
uniform float     u_opacity;
uniform vec3      u_cam_world_pos;
uniform float     u_box_size;

#define MAX_STEPS 256
#define STEP_SIZE 0.005     // volume step size

vec4 transferFunction(float density) {
    // Baixa densidade: azul escuro/ciano; média: magenta/rosa pálido; alta: branco e dourado
    // Simulando emissão térmica e poeira quente da teia cósmica.
    vec3 cool  = vec3(0.01, 0.05, 0.15);
    vec3 warm  = vec3(0.6, 0.1, 0.4);
    vec3 hot   = vec3(1.0, 0.8, 0.3);
    vec3 core  = vec3(1.0, 1.0, 1.0);

    float t = clamp(density * u_density_scale, 0.0, 1.0);
    
    vec3 col_mix = mix(cool, warm, smoothstep(0.0, 0.4, t));
    col_mix = mix(col_mix, hot, smoothstep(0.4, 0.8, t));
    col_mix = mix(col_mix, core, smoothstep(0.8, 1.0, t));
    
    // Suaviza muito o limiar base pra não poluir todo o vazio de cor e criar fios
    float alpha = smoothstep(0.05, 0.8, t) * u_opacity_scale * u_opacity;
    
    // Evita acumulação absurda mas mantém denso
    return vec4(col_mix, min(alpha * 0.1, 0.5));
}

void main() {
    // O raio parte do olho da câmera (que está na origem do view_space, ou seja, 0,0,0)
    vec3 ray_origin = vec3(0.0);
    vec3 ray_dir = normalize(v_ray_dir); // a posição interpolada do quad de tela atua como vetor diretor

    // Mascaramos para a caixa do volume no espaço do mundo (relativo à câmera)
    // O volume vai de center - box_size/2 até center + box_size/2 em World Space
    // Como estamos na origem, a coordenada MÍNIMA da caixa em relação a nós é:
    vec3 box_min = (-u_box_size * 0.5) - u_cam_world_pos;
    vec3 box_max = ( u_box_size * 0.5) - u_cam_world_pos;

    // Interseção simples com caixa alinhada aos eixos
    vec3 inv_dir = 1.0 / (ray_dir + 1e-8);
    vec3 t0 = (box_min - ray_origin) * inv_dir;
    vec3 t1 = (box_max - ray_origin) * inv_dir;
    vec3 tmin3 = min(t0, t1);
    vec3 tmax3 = max(t0, t1);
    float t_enter = max(max(tmin3.x, tmin3.y), tmin3.z);
    float t_exit  = min(min(tmax3.x, tmax3.y), tmax3.z);

    if (t_enter > t_exit) discard;

    t_enter = max(t_enter, 0.0);

    vec4 accum = vec4(0.0);
    float t = t_enter;
    float step_world = u_box_size * STEP_SIZE;
    
    for (int i = 0; i < MAX_STEPS; ++i) {
        if (t > t_exit || accum.a > 0.99) break;
        vec3 pos_world = ray_origin + ray_dir * t;
        
        // Converter de volta para [0,1]^3 para amostrar textura 3D
        vec3 pos_vol = (pos_world + u_cam_world_pos) / u_box_size + 0.5;
        // Evitar sampling fora da borda gerando artefatos
        if(any(lessThan(pos_vol, vec3(0.0))) || any(greaterThan(pos_vol, vec3(1.0)))) {
            t += step_world;
            continue;
        }

        float density = texture(u_density_tex, pos_vol).r;
        if(density > 0.005) {    
            vec4 sample_col = transferFunction(density);
            // Composição frente-para-trás
            accum.rgb += (1.0 - accum.a) * sample_col.a * sample_col.rgb;
            accum.a   += (1.0 - accum.a) * sample_col.a;
        }
        t += step_world;
    }

    frag_color = accum;
}
