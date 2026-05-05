#pragma once
// src/render/StromgrenRenderer.hpp — Esfera de ionização de Strömgren (Regimes 7–8).
// Regra 0.1: guarda regime >= REGIME_REIONIZATION.
// Regra 0.5: zero alocação em Render(). 3 layers aditivos por estrela ionizante.

#include "ICosmicRenderer.hpp"
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class StromgrenRenderer : public ICosmicRenderer {
public:
    StromgrenRenderer() = default;
    ~StromgrenRenderer() override { Shutdown(); }

    bool Init(QualityTier quality) override;
    void OnQualityChanged(QualityTier new_quality) override;

    void Render(const ParticlePool& particles,
                int regime,
                const Camera& cam,
                float sim_time_myr) override;

    void Shutdown() override;
    void SetScreenSize(int w, int h) { screen_w_ = w; screen_h_ = h; }

private:
    struct Config {
        float base_radius;   // raio base em unidades de sim
        int   max_sources;   // estrelas ionizantes máximas
    };
    static constexpr Config CONFIGS[3] = {
        { 0.8f,  50 },  // SAFE
        { 1.2f, 150 },  // MEDIUM
        { 1.6f, 300 },  // HIGH
    };

    void BuildLayerBuffers(const ParticlePool& particles, int regime);

    static GLuint CompileShader(GLenum type, const std::string& path);
    static bool   LinkProgram(GLuint prog, GLuint vs, GLuint fs);

    GLuint vao_     = 0;
    GLuint vbo_pos_ = 0;   // vec3  position (posição da estrela)
    GLuint vbo_rad_ = 0;   // float radius
    GLuint vbo_lay_ = 0;   // float layer (0,1,2)
    GLuint prog_    = 0;

    GLint uloc_view_   = -1;
    GLint uloc_proj_   = -1;
    GLint uloc_screen_ = -1;

    // 3 layers por estrela — pré-alocado
    std::vector<float> pos_buf_;  // x,y,z interleaved
    std::vector<float> rad_buf_;
    std::vector<float> lay_buf_;

    Config  config_    = CONFIGS[1];
    int     draw_count_= 0;
    int     screen_w_  = 1280;
    int     screen_h_  = 720;
    bool    initialized_ = false;
};
