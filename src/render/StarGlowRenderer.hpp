#pragma once
// src/render/StarGlowRenderer.hpp — Brilho estelar billboard (Regimes 7–8).
// Regra 0.1: guarda regime >= REGIME_REIONIZATION.
// Regra 0.5: zero alocação em Render().

#include "ICosmicRenderer.hpp"
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class StarGlowRenderer : public ICosmicRenderer {
public:
    StarGlowRenderer() = default;
    ~StarGlowRenderer() override { Shutdown(); }

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
        float base_size_px;  // tamanho base do ponto em pixels
        int   max_stars;
    };
    static constexpr Config CONFIGS[3] = {
        {  8.0f, 200 },  // SAFE
        { 12.0f, 500 },  // MEDIUM
        { 16.0f, 730 },  // HIGH
    };

    void UploadStars(const ParticlePool& particles);

    static GLuint CompileShader(GLenum type, const std::string& path);
    static bool   LinkProgram(GLuint prog, GLuint vs, GLuint fs);

    GLuint vao_      = 0;
    GLuint vbo_pos_  = 0;  // vec3 position
    GLuint vbo_lum_  = 0;  // float luminosity
    GLuint vbo_sta_  = 0;  // int   star_state
    GLuint vbo_temp_ = 0;  // float temp_particle
    GLuint profile_tex_ = 0;
    GLuint prog_     = 0;

    GLint uloc_view_      = -1;
    GLint uloc_proj_      = -1;
    GLint uloc_base_size_ = -1;
    GLint uloc_screen_    = -1;
    GLint uloc_profile_tex_ = -1;
    int   profile_tex_width_ = 1;
    int   profile_tex_height_ = 1;
    bool  profile_tex_loaded_ = false;

    std::vector<float> pos_buf_;
    std::vector<float> lum_buf_;
    std::vector<int>   sta_buf_;
    std::vector<float> temp_buf_;

    Config  config_      = CONFIGS[1];
    int     star_count_  = 0;
    int     screen_w_    = 1280;
    int     screen_h_    = 720;
    bool    initialized_ = false;
};
