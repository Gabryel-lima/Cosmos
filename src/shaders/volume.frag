#version 430 core
// volume.frag — Raymarching frente-para-trás pelo campo de densidade 3D.

in vec2 v_uv;
in vec3 v_ray_dir;

out vec4 frag_color;

uniform sampler3D u_density_tex;
uniform sampler3D u_ionization_tex;
uniform sampler3D u_emissivity_tex;
uniform int       u_regime;
uniform float     u_density_scale;
uniform float     u_opacity_scale;
uniform float     u_opacity;
uniform float     u_edge_boost;
uniform vec3      u_cam_world_pos;
uniform float     u_box_size;
uniform vec3      u_color_cold;
uniform vec3      u_color_warm;
uniform vec3      u_color_hot;
uniform vec3      u_color_core;

#ifndef COSMOS_VOLUME_MAX_STEPS
#define COSMOS_VOLUME_MAX_STEPS 256
#endif

#ifndef COSMOS_VOLUME_STEP_SIZE
#define COSMOS_VOLUME_STEP_SIZE 0.005
#endif

#ifndef COSMOS_VOLUME_FBM_OCTAVES
#define COSMOS_VOLUME_FBM_OCTAVES 4
#endif

#ifndef COSMOS_VOLUME_ALPHA_COMPENSATION
#define COSMOS_VOLUME_ALPHA_COMPENSATION 1.0
#endif

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    return mix(nxy0, nxy1, f.z);
}

float fbm(vec3 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < COSMOS_VOLUME_FBM_OCTAVES; ++i) {
        value += noise3(p) * amplitude;
        p = p * 2.03 + vec3(11.0, 7.0, 5.0);
        amplitude *= 0.5;
    }
    return value;
}

vec4 transferFunction(float density, float ionization, float emissivity, float edge, vec3 pos_vol) {
    float t = clamp(density * u_density_scale, 0.0, 1.0);
    float xion = clamp(ionization, 0.0, 1.0);
    float source = clamp(emissivity, 0.0, 1.0);
    float macro_noise = fbm(pos_vol * mix(6.0, 14.0, float(u_regime >= 7)) + vec3(0.0, 1.3, 2.1));
    float wispy_noise = fbm(pos_vol.zyx * 17.0 + vec3(4.0, 9.0, 2.0));
    float dust = clamp(0.42 + macro_noise * 0.85 + wispy_noise * 0.35, 0.0, 1.6);
    float dense_core = smoothstep(0.16, 0.92, t + source * 0.25);
    float void_mask = 1.0 - smoothstep(0.03, 0.24, t + macro_noise * 0.08);
    
    vec3 col_mix = mix(u_color_cold, u_color_warm, smoothstep(0.0, 0.4, t));
    col_mix = mix(col_mix, u_color_hot, smoothstep(0.4, 0.8, t));
    col_mix = mix(col_mix, u_color_core, smoothstep(0.8, 1.0, t));
    vec3 ionized_glow = mix(vec3(0.12, 0.34, 0.85), vec3(0.80, 0.95, 1.00), smoothstep(0.15, 0.95, xion));
    vec3 source_glow = mix(vec3(0.95, 0.52, 0.16), u_color_core, smoothstep(0.05, 0.95, source));
    float bubble_shell = exp(-24.0 * (xion - 0.58) * (xion - 0.58));
    
    float alpha = 0.0;
    if (u_regime == 6) {
        vec3 neutral_smoke = mix(u_color_cold, u_color_warm, smoothstep(0.04, 0.58, t + macro_noise * 0.12));
        neutral_smoke = mix(neutral_smoke, u_color_hot, smoothstep(0.55, 1.0, t) * 0.28);
        col_mix = neutral_smoke * mix(0.38, 1.22, dust);
        col_mix *= 0.60 + dense_core * 0.30;
        col_mix *= 1.0 - void_mask * 0.18;
        col_mix += edge * u_color_warm * 0.10;
        alpha = smoothstep(0.03, 0.68, t + macro_noise * 0.10) * (0.12 + dust * 0.12);
        alpha += dense_core * 0.08;
    } else if (u_regime == 7) {
        vec3 neutral_smoke = mix(u_color_cold, u_color_warm, smoothstep(0.03, 0.52, t + macro_noise * 0.10));
        vec3 ion_shell = mix(vec3(0.30, 0.46, 0.74), vec3(0.86, 0.95, 1.00), smoothstep(0.15, 0.95, xion));
        col_mix = neutral_smoke * mix(0.40, 1.12, dust);
        col_mix = mix(col_mix, ion_shell, smoothstep(0.16, 0.86, xion) * 0.34);
        col_mix += ion_shell * bubble_shell * 0.22;
        col_mix += source_glow * (0.10 + 0.30 * source);
        col_mix += edge * mix(u_color_warm, ion_shell, 0.65) * 0.20;
        alpha = smoothstep(0.025, 0.62, t + macro_noise * 0.09) * (0.11 + dust * 0.10);
        alpha += smoothstep(0.18, 0.95, xion) * 0.06;
        alpha += source * 0.08;
    } else if (u_regime >= 8) {
        vec3 warm_filament = mix(u_color_cold, u_color_warm, smoothstep(0.02, 0.44, t + macro_noise * 0.08));
        warm_filament = mix(warm_filament, u_color_hot, smoothstep(0.40, 0.90, t + source * 0.18));
        col_mix = warm_filament * mix(0.42, 1.10, dust);
        col_mix = mix(col_mix, ionized_glow, smoothstep(0.24, 0.92, xion) * 0.18);
        col_mix += source_glow * (0.14 + 0.48 * source);
        col_mix += edge * mix(u_color_warm, u_color_hot, 0.6) * 0.26;
        alpha = smoothstep(0.03, 0.74, t + macro_noise * 0.07) * (0.10 + dust * 0.09);
        alpha += source * 0.10 + dense_core * 0.05;
    } else {
        col_mix = mix(col_mix, ionized_glow, smoothstep(0.18, 0.92, xion) * 0.55);
        col_mix += source_glow * (0.18 + 0.52 * source);
        col_mix += edge * mix(u_color_warm, u_color_hot, 0.5) * 0.35;
        col_mix += ionized_glow * bubble_shell * 0.20;
        float alpha_low = (u_regime >= 6) ? 0.10 : 0.05;
        alpha = smoothstep(alpha_low, 0.8, t) * u_opacity_scale * u_opacity;
        alpha *= mix(0.8, 1.2, clamp(edge, 0.0, 1.0));
        alpha += xion * ((u_regime >= 7) ? 0.10 : 0.03) * u_opacity_scale;
        alpha += source * 0.08 * u_opacity_scale;
    }
    
    return vec4(col_mix, min(alpha * 0.1, 0.6));
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
    float step_world = u_box_size * COSMOS_VOLUME_STEP_SIZE;
    float t = t_enter + step_world * hash13(vec3(v_uv, t_enter + u_regime * 0.17));
    
    for (int i = 0; i < COSMOS_VOLUME_MAX_STEPS; ++i) {
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
        float ionization = texture(u_ionization_tex, pos_vol).r;
        float emissivity = texture(u_emissivity_tex, pos_vol).r;
        if(density > 0.002) {
            vec3 texel = 1.0 / vec3(textureSize(u_density_tex, 0));
            vec3 ray_tex_step = normalize(abs(ray_dir) + vec3(1e-5)) * texel * 2.0;
            float ahead_density = texture(u_density_tex, clamp(pos_vol + ray_tex_step, 0.0, 1.0)).r;
            float ahead_ionization = texture(u_ionization_tex, clamp(pos_vol + ray_tex_step, 0.0, 1.0)).r;
            float edge = clamp((abs(ahead_density - density) + abs(ahead_ionization - ionization) * 0.85) * u_edge_boost * 4.0, 0.0, 1.0);
            vec4 sample_col = transferFunction(density, ionization, emissivity, edge, pos_vol);
            sample_col.a = min(sample_col.a * COSMOS_VOLUME_ALPHA_COMPENSATION, 0.9);
            // Composição frente-para-trás
            accum.rgb += (1.0 - accum.a) * sample_col.a * sample_col.rgb;
            accum.a   += (1.0 - accum.a) * sample_col.a;
        }
        t += step_world;
    }

    frag_color = accum;
}
