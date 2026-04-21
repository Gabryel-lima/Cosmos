#pragma once
// src/render/PostProcess.hpp — Bloom, mapeamento de tons HDR, gradação de cor.
#include <glad/gl.h>

class PostProcess {
public:
    bool init(int width, int height);
    void resize(int width, int height);
    /// Aplica bloom e mapeamento de tons ACES.
    /// @param hdr_tex  textura de cor HDR FP16 de entrada
    /// @param bloom_strength  intensidade aditiva do bloom (0 = desativado)
    void apply(GLuint hdr_tex, float bloom_strength = 0.3f, float exposure = 1.0f);
    /// O índice do regime ativo define a LUT de cor (0..4).
    void setRegimeLUT(int regime_index);

private:
    int width_ = 0, height_ = 0;
    int regime_lut_ = 0;
    GLuint bloom_program_   = 0;
    GLuint tonemap_program_ = 0;
    GLuint quad_vao_        = 0;
    GLuint quad_vbo_        = 0;
    GLuint bloom_fbo_[2]    = {};
    GLuint bloom_tex_[2]    = {};
};
