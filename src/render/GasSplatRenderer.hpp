#pragma once
// src/render/GasSplatRenderer.hpp — Renderizador de Gaussian splat para gás.
// Ativo em Regimes 6 (Dark Ages), 7 (Reionização) e 8 (Estrutura).
// Regra 0.1: guarda regime >= REGIME_DARK_AGES.
// Regra 0.5: zero alocação em Render().

#include "ICosmicRenderer.hpp"
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class GasSplatRenderer : public ICosmicRenderer {
public:
    GasSplatRenderer() = default;
    ~GasSplatRenderer() override { Shutdown(); }

    bool Init(QualityTier quality) override;
    void OnQualityChanged(QualityTier new_quality) override;

    void Render(const ParticlePool& particles,
                int regime,
                const Camera& cam,
                float sim_time_myr) override;

    void Shutdown() override;

    /// Chamar uma vez por frame antes de Render() para atualizar flags de ionização.
    /// Não aloca memória (Regra 0.5).
    void UpdateIonization(ParticlePool& particles,
                          float avg_stellar_luminosity,
                          int regime);

    void SetScreenSize(int w, int h) { screen_w_ = w; screen_h_ = h; }

private:
    struct Config {
        float sigma_px;
        float alpha_scale;
        int   max_splats;
        bool  use_soft_edge;
    };
    static constexpr Config CONFIGS[3] = {
        {  8.0f, 0.6f, 500, false },  // SAFE
        { 16.0f, 0.8f, 730, false },  // MEDIUM
        { 32.0f, 1.0f, 730, true  },  // HIGH
    };

    void UploadGasParticles(const ParticlePool& particles);

    // Shader helpers
    static GLuint CompileShader(GLenum type, const std::string& path);
    static bool   LinkProgram(GLuint prog, GLuint vs, GLuint fs);
    static std::string ResolvePath(const std::string& relative);

    // GL resources
    GLuint vao_  = 0;
    GLuint vbo_pos_   = 0;   // vec3  position
    GLuint vbo_smth_  = 0;   // float smoothing_length
    GLuint vbo_temp_  = 0;   // float temperature_kev
    GLuint vbo_ion_   = 0;   // int   ionized
    GLuint profile_tex_ = 0;
    GLuint prog_ = 0;

    // Uniforms cached
    GLint uloc_view_       = -1;
    GLint uloc_proj_       = -1;
    GLint uloc_sigma_px_   = -1;
    GLint uloc_screen_size_= -1;
    GLint uloc_profile_tex_= -1;
    GLint uloc_alpha_scale_= -1;
    int   profile_tex_width_ = 1;
    int   profile_tex_height_ = 1;
    bool  profile_tex_loaded_ = false;

    // Pré-alocados em Init — zero alloc em Render (Regra 0.5)
    std::vector<float> pos_buf_;   // x,y,z intercalados — max_splats * 3
    std::vector<float> smth_buf_;
    std::vector<float> temp_buf_;
    std::vector<int>   ion_buf_;

    Config  config_      = CONFIGS[1];
    int     gas_count_   = 0;
    int     screen_w_    = 1280;
    int     screen_h_    = 720;
    bool    initialized_ = false;
};
