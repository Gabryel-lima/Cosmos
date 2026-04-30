#version 430 core
// particle.frag — Billboard brilhante com bordas suaves para partículas.

in vec2  v_uv;
in vec4  v_color; // rgb = color, a = shader tag (1.0 = gluon)
in float v_size;

out vec4 frag_color;

uniform float u_opacity;
uniform int   u_regime;
uniform vec3  u_particle_tint;
uniform float u_halo_softness;
uniform float u_core_boost;
uniform float u_sparkle_gain;
uniform float u_streak_gain;
uniform float u_halo_axis_ratio;

const float TAG_GLUON = 1.0;
const float TAG_PHOTON = 2.0;
const float TAG_GAS = 3.0;
const float TAG_STAR = 4.0;
const float TAG_BLACK_HOLE = 5.0;
const float TAG_HALO = 6.0;

float sparkleMask(vec2 p) {
    float a = atan(p.y, p.x);
    float radial = length(p);
    float spokes = 0.5 + 0.5 * cos(a * 6.0);
    return spokes * exp(-8.0 * radial * radial);
}

float sizeEnergyCompensation(float particleSize, int regime) {
    float regime_scale = (regime >= 6) ? 6.0 : ((regime >= 4) ? 10.0 : 18.0);
    float metric = max(particleSize * regime_scale, 0.0);
    return inversesqrt(1.0 + metric * metric * 0.75);
}

void main() {
    // Queda radial suave centralizada em (0.5, 0.5)
    vec2 center = v_uv - 0.5;
    float d = length(center) * 2.0;   // 0..1 do centro à borda
    if (d > 1.0) discard;

    // Perfil gaussiano com cauda de brilho
    float alpha = exp(-(3.5 + u_halo_softness) * d * d);

    // Núcleo interno brilhante
    float core = exp(-(10.0 + 4.0 * u_core_boost) * d * d);

    float tag = v_color.a;
    bool isGluon = abs(tag - TAG_GLUON) < 0.25;
    bool isPhoton = abs(tag - TAG_PHOTON) < 0.25;
    bool isGas = abs(tag - TAG_GAS) < 0.25;
    bool isStar = abs(tag - TAG_STAR) < 0.25;
    bool isBlackHole = abs(tag - TAG_BLACK_HOLE) < 0.25;
    bool isHalo = abs(tag - TAG_HALO) < 0.25;

    vec3 color = v_color.rgb * mix(vec3(1.0), u_particle_tint, 0.35);
    float vivid = 1.0 - smoothstep(0.18, 0.72, alpha);
    float maxc = max(max(color.r, color.g), color.b);
    vec3 hue_preserved = (maxc > 1e-4) ? (color / maxc) * max(maxc, 0.22) : color;
    color = mix(color, hue_preserved * (1.0 + 0.18 * vivid), 0.28 * vivid);

    float size_comp = sizeEnergyCompensation(v_size, u_regime);
    float contour_mix = clamp(1.0 - size_comp, 0.0, 1.0);
    alpha *= mix(1.0, 0.55, contour_mix) * size_comp;
    core *= mix(1.0, 0.72, contour_mix) * size_comp;
    float rim = exp(-24.0 * (d - mix(0.78, 0.68, contour_mix)) * (d - mix(0.78, 0.68, contour_mix)))
              * (0.12 + 0.22 * contour_mix);

    if (u_regime >= 6) {
        color *= mix(vec3(0.92), vec3(1.04, 1.03, 1.08), smoothstep(0.15, 0.95, core));
    }

    // Default glow/bright for quarks/others
    vec3 glow = color * alpha;
    vec3 bright = min(color * mix(3.0, 2.1, contour_mix), vec3(3.0)) * core;
    vec3 rim_light = color * (0.45 + 0.30 * contour_mix) * rim;

    // For gluons, draw a distinctive ring + inner core to convey mediator shape
    if (isGluon) {
        // Ring profile: emphasize an annulus around radius ~0.35..0.6
        float ring = smoothstep(0.32, 0.38, d) - smoothstep(0.52, 0.58, d);
        float ring_soft = smoothstep(0.28, 0.38, d) * (1.0 - smoothstep(0.48, 0.58, d));
        float ring_alpha = 0.9 * ring + 0.35 * ring_soft;

        // Add a faint radial streak to hint at directional exchange (visual only)
        float streak = pow(abs(center.x) * 2.0, 6.0) * (1.0 - smoothstep(0.0, 0.9, d));
        vec3 ring_color = mix(color * 1.2, vec3(1.0, 0.9, 0.6), 0.35);

        vec3 ring_glow = ring_color * ring_alpha * 1.6;
        vec3 core_glow = color * (core * 1.4);

        frag_color = vec4((ring_glow + core_glow + rim_light * 0.5) * u_opacity,
                          (ring_alpha + alpha * 0.6) * u_opacity);
        return;
    }

    if (isPhoton) {
        float airy = exp(-5.0 * d * d) + 0.35 * exp(-28.0 * (d - 0.55) * (d - 0.55));
        float streak = pow(abs(center.x) + abs(center.y), 6.0) * exp(-10.0 * d * d);
        vec3 photon_color = mix(color, vec3(1.0), 0.28);
        frag_color = vec4((photon_color * airy * (1.15 + 0.2 * size_comp) + vec3(1.0) * streak * u_streak_gain + rim_light * 0.35) * u_opacity,
                          min(airy * 0.95, 1.0) * u_opacity);
        return;
    }

    if (isGas) {
        float cloud = exp(-2.0 * d * d);
        float rim = exp(-22.0 * (d - 0.72) * (d - 0.72));
        vec3 gas_color = mix(color, color.bgr * vec3(0.9, 1.0, 1.1), 0.18);
        frag_color = vec4((gas_color * cloud * 0.72 * size_comp + gas_color * rim * 0.65 + rim_light * 0.5) * u_opacity,
                          min(cloud * 0.55 * size_comp + rim * 0.28, 0.82) * u_opacity);
        return;
    }

    if (isStar) {
        float corona = exp(-2.8 * d * d);
        float sparkle = sparkleMask(center) * u_sparkle_gain;
        vec3 star_color = mix(color, vec3(1.0, 0.98, 0.92), 0.22);
        frag_color = vec4((star_color * corona * 1.18 * size_comp + vec3(1.0, 0.95, 0.82) * sparkle * 2.2 + rim_light * 0.7) * u_opacity,
                          min(corona * 0.95 + sparkle, 1.0) * u_opacity);
        return;
    }

    if (isBlackHole) {
        float ring = smoothstep(0.26, 0.34, d) - smoothstep(0.62, 0.70, d);
        float core_shadow = 1.0 - smoothstep(0.0, 0.18, d);
        vec3 accretion = mix(color, vec3(1.0, 0.82, 0.56), 0.45) * ring * (1.2 + 0.6 * u_core_boost);
        vec3 lens = vec3(0.03, 0.04, 0.07) * (1.0 - core_shadow);
        frag_color = vec4((accretion * size_comp + lens + rim_light * 0.35) * u_opacity,
                          min(ring * 0.9 + (1.0 - core_shadow) * 0.2, 0.95) * u_opacity);
        return;
    }

    if (isHalo) {
        vec2 halo_uv = center;
        halo_uv.x *= clamp(u_halo_axis_ratio, 0.65, 2.4);
        halo_uv.y *= mix(1.18, 0.82, clamp(u_halo_axis_ratio - 1.0, -0.6, 1.4));
        float halo_d = length(halo_uv) * 2.0;
        float shell = smoothstep(0.30, 0.42, halo_d) - smoothstep(0.76, 0.92, halo_d);
        float inner_shell = smoothstep(0.18, 0.26, halo_d) - smoothstep(0.48, 0.60, halo_d);
        float haze = exp(-2.1 * halo_d * halo_d) * 0.16;
        vec3 halo_glow = mix(color, vec3(1.0), 0.12);
        frag_color = vec4((halo_glow * shell * 1.18 + halo_glow * inner_shell * 0.55 + halo_glow * haze + rim_light * 0.75) * u_opacity,
                          min(shell * 0.68 + inner_shell * 0.18 + haze, 0.74) * u_opacity);
        return;
    }

    float sparkle = sparkleMask(center) * u_sparkle_gain * smoothstep(0.0, 0.55, 1.0 - d);
    float streak = pow(abs(center.x * center.y) * 4.0, 2.0) * exp(-6.0 * d * d) * u_streak_gain;
    frag_color = vec4((glow + bright + rim_light + vec3(1.0) * (sparkle + streak)) * u_opacity,
                      min(alpha + sparkle * 0.25, 1.0) * u_opacity);
}
