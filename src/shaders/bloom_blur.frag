#version 430 core
// bloom_blur.frag — Desfoque gaussiano separável para bloom.

in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_tex;
uniform bool      u_horizontal;
uniform vec2      u_texel_size;   // 1.0/resolução

const float weights[5] = float[](0.2270270, 0.1945945, 0.1216216, 0.0540540, 0.0162162);

void main() {
    vec3 result = texture(u_tex, v_uv).rgb * weights[0];
    vec2 dir = u_horizontal ? vec2(1.0, 0.0) : vec2(0.0, 1.0);

    for (int i = 1; i < 5; ++i) {
        vec2 off = float(i) * dir * u_texel_size;
        result += texture(u_tex, v_uv + off).rgb * weights[i];
        result += texture(u_tex, v_uv - off).rgb * weights[i];
    }
    frag_color = vec4(result, 1.0);
}
