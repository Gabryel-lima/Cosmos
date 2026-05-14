#version 430 core
// volume.frag — Raymarching frente-para-trás pelo campo de densidade 3D.

in vec2 v_uv;
in vec3 v_ray_dir;

out vec4 frag_color;

uniform sampler3D u_density_tex;
uniform sampler3D u_ionization_tex;
uniform sampler3D u_emissivity_tex;
uniform sampler2D u_macro_lookup_tex;
uniform int       u_regime;
uniform int       u_max_steps;
uniform float     u_step_size;
uniform int       u_fbm_octaves;
uniform float     u_quality_band;
uniform float     u_edge_sample_scale;
uniform float     u_density_threshold;
uniform float     u_density_scale;
uniform float     u_opacity_scale;
uniform float     u_opacity;
uniform float     u_edge_boost;
uniform float     u_macro_lookup_strength;
uniform float     u_macro_lookup_scale;
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

#ifndef COSMOS_VOLUME_QUALITY_BAND
#define COSMOS_VOLUME_QUALITY_BAND 2
#endif

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise3(vec3 p) {
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
        if (i >= u_fbm_octaves) break;
        value += valueNoise3(p) * amplitude;
        p = p * 2.03 + vec3(11.0, 7.0, 5.0);
        amplitude *= 0.5;
    }
    return value;
}

float ridgeNoise(float value) {
    return 1.0 - abs(value * 2.0 - 1.0);
}

float foldedNebula(vec3 p, float tile_scale) {
    vec3 q = abs(fract(p * tile_scale) * 2.0 - 1.0);
    float accum = 0.0;
    float amplitude = 0.65;
    for (int i = 0; i < 3; ++i) {
        float layer = ridgeNoise(valueNoise3(q * (2.2 + float(i) * 0.9) + vec3(1.7, 3.1, 5.3)));
        accum += layer * amplitude;
        q = abs(q.yzx * 1.85 + vec3(0.21, 0.37, 0.53)) - 0.32;
        amplitude *= 0.55;
    }
    return accum;
}

float sampleMacroLookup(vec3 pos_vol) {
    vec2 uv0 = fract(pos_vol.xz * u_macro_lookup_scale + vec2(pos_vol.y * 0.31, pos_vol.x * 0.17));
    vec2 uv1 = fract(pos_vol.yx * (u_macro_lookup_scale * 0.61) + vec2(0.13, 0.47));
    float layer0 = texture(u_macro_lookup_tex, uv0).r;
    float layer1 = texture(u_macro_lookup_tex, uv1).r;
    return mix(layer0, layer1, 0.35);
}

vec4 transferFunction(float density, float ionization, float emissivity, float edge, vec3 pos_vol) {
    float t = clamp(density * u_density_scale, 0.0, 1.0);
    float xion = clamp(ionization, 0.0, 1.0);
    float source = clamp(emissivity, 0.0, 1.0);
    float quality_mix = clamp(u_quality_band / 4.0, 0.0, 1.0);
    float macro_noise = fbm(pos_vol * mix(6.0, 14.0, float(u_regime >= 7)) + vec3(0.0, 1.3, 2.1));
    float wispy_noise = fbm(pos_vol.zyx * 17.0 + vec3(4.0, 9.0, 2.0));
    float folded = foldedNebula(pos_vol + vec3(0.07, 0.19, 0.13), mix(3.0, 5.4, float(u_regime >= 7)));
    float filaments = ridgeNoise(clamp(fbm(pos_vol * 10.0 + vec3(folded, macro_noise, wispy_noise)), 0.0, 1.0));
    float baked_lookup = sampleMacroLookup(pos_vol);
    float baked_bias = (baked_lookup - 0.5) * u_macro_lookup_strength;
    macro_noise = clamp(macro_noise + baked_bias * 0.55, 0.0, 1.0);
    wispy_noise = clamp(wispy_noise + baked_bias * 0.35, 0.0, 1.0);
    folded = clamp(folded + baked_lookup * 0.25 * u_macro_lookup_strength, 0.0, 1.6);
    filaments = clamp(mix(filaments,
                          filaments * mix(0.72, 1.34, baked_lookup),
                          u_macro_lookup_strength),
                      0.0, 1.0);
    float dust = clamp(0.34 + macro_noise * 0.72 + wispy_noise * 0.28 + folded * 0.34 + filaments * 0.18, 0.0, 1.7);
    float dense_core = smoothstep(0.16, 0.92, t + source * 0.25);
    float void_mask = 1.0 - smoothstep(0.03, 0.24, t + macro_noise * 0.08 - folded * 0.05);
    float stratified = smoothstep(0.12, 0.92, folded * 0.55 + macro_noise * 0.25 + filaments * 0.20);
    float depth_fade = smoothstep(0.05, 0.95, pos_vol.y * (0.88 + quality_mix * 0.22) + macro_noise * 0.04);
    float back_shadow = smoothstep(0.18, 0.86, t * 0.55 + folded * 0.18 + wispy_noise * 0.10);
    
    vec3 col_mix = mix(u_color_cold, u_color_warm, smoothstep(0.0, 0.4, t));
    col_mix = mix(col_mix, u_color_hot, smoothstep(0.4, 0.8, t));
    col_mix = mix(col_mix, u_color_core, smoothstep(0.8, 1.0, t));
    vec3 ionized_glow = mix(vec3(0.12, 0.34, 0.85), vec3(0.80, 0.95, 1.00), smoothstep(0.15, 0.95, xion));
    vec3 source_glow = mix(vec3(0.95, 0.52, 0.16), u_color_core, smoothstep(0.05, 0.95, source));
    float bubble_shell = exp(-24.0 * (xion - 0.58) * (xion - 0.58));
    float front_patch = smoothstep(0.18, 0.92, xion + folded * 0.16 - filaments * 0.10);
    float front_shell = exp(-20.0 * (xion - (0.50 + filaments * 0.10)) * (xion - (0.50 + filaments * 0.10)));
    
    float alpha = 0.0;
    if (u_regime == 6) {
        vec3 neutral_smoke = mix(u_color_cold, u_color_warm, smoothstep(0.03, 0.52, t + macro_noise * 0.10 + folded * 0.06));
        vec3 lane_tint = mix(neutral_smoke, u_color_hot, smoothstep(0.42, 1.0, filaments) * (0.18 + quality_mix * 0.10));
        vec3 cold_shadow = mix(u_color_cold * 0.42, u_color_warm * 0.24, depth_fade);
        col_mix = lane_tint * mix(0.26, 1.22, dust);
        col_mix *= 0.50 + dense_core * 0.26 + filaments * 0.08 + stratified * 0.12;
        col_mix = mix(col_mix, cold_shadow, back_shadow * 0.18);
        col_mix *= 1.0 - void_mask * 0.24;
        col_mix += edge * mix(u_color_warm, u_color_hot, 0.35) * (0.06 + 0.12 * filaments + quality_mix * 0.06);
        alpha = smoothstep(0.018, 0.68, t + macro_noise * 0.11 + folded * 0.08 + stratified * 0.06) * (0.08 + dust * 0.13);
        alpha += dense_core * 0.08 + filaments * 0.04 + stratified * 0.03;
    } else if (u_regime == 7) {
        vec3 neutral_smoke = mix(u_color_cold, u_color_warm, smoothstep(0.03, 0.48, t + macro_noise * 0.08 + folded * 0.05));
        vec3 ion_shell = mix(vec3(0.30, 0.46, 0.74), vec3(0.86, 0.95, 1.00), smoothstep(0.15, 0.95, xion));
        vec3 patch_glow = mix(vec3(0.36, 0.58, 0.96), vec3(0.92, 0.98, 1.00), front_patch);
        col_mix = neutral_smoke * mix(0.38, 1.08, dust);
        col_mix = mix(col_mix, ion_shell, front_patch * 0.38);
        col_mix += patch_glow * front_shell * (0.20 + folded * 0.16 + quality_mix * 0.08);
        col_mix += ion_shell * bubble_shell * 0.18;
        col_mix += source_glow * (0.14 + 0.38 * source);
        col_mix += edge * mix(u_color_warm, patch_glow, 0.72) * (0.20 + 0.12 * filaments);
        col_mix = mix(col_mix, u_color_cold * 0.28, void_mask * 0.10);
        alpha = smoothstep(0.020, 0.60, t + macro_noise * 0.10 + folded * 0.06 + stratified * 0.03) * (0.10 + dust * 0.10);
        alpha += front_patch * 0.08 + front_shell * 0.05;
        alpha += source * 0.10 + bubble_shell * 0.03;
    } else if (u_regime >= 8) {
        vec3 warm_filament = mix(u_color_cold, u_color_warm, smoothstep(0.02, 0.42, t + macro_noise * 0.06 + filaments * 0.10));
        warm_filament = mix(warm_filament, u_color_hot, smoothstep(0.38, 0.92, t + source * 0.18 + folded * 0.08));
        float dust_lane = smoothstep(0.40, 0.96, filaments + folded * 0.26) * (1.0 - smoothstep(0.16, 0.80, source));
        float caustic = ridgeNoise(clamp(fbm(pos_vol * mix(14.0, 20.0, quality_mix) + vec3(0.7, 1.1, 1.9)), 0.0, 1.0));
        col_mix = warm_filament * mix(0.38, 1.12, dust);
        col_mix = mix(col_mix, ionized_glow, smoothstep(0.24, 0.92, xion) * 0.20);
        col_mix += source_glow * (0.18 + 0.56 * source);
        col_mix += edge * mix(u_color_warm, u_color_hot, 0.6) * (0.22 + 0.10 * filaments + caustic * 0.06);
        col_mix += u_color_hot * caustic * source * (0.08 + quality_mix * 0.08);
        col_mix *= 1.0 - dust_lane * 0.20;
        alpha = smoothstep(0.025, 0.72, t + macro_noise * 0.07 + folded * 0.06 + caustic * 0.04) * (0.10 + dust * 0.09);
        alpha += source * 0.12 + dense_core * 0.06 + dust_lane * 0.05;
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
    
    float quality_alpha = mix(0.88, 1.06, quality_mix);
    return vec4(col_mix, min(alpha * 0.1 * quality_alpha, 0.64));
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
    float step_world = u_box_size * u_step_size;
    float t = t_enter + step_world * hash13(vec3(v_uv, t_enter + u_regime * 0.17));
    
    for (int i = 0; i < COSMOS_VOLUME_MAX_STEPS; ++i) {
        if (i >= u_max_steps || t > t_exit || accum.a > 0.99) break;
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
        if(density > u_density_threshold) {
            vec3 texel = 1.0 / vec3(textureSize(u_density_tex, 0));
            vec3 ray_tex_step = normalize(abs(ray_dir) + vec3(1e-5)) * texel * u_edge_sample_scale;
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
