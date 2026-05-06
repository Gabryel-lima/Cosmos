#pragma once
// src/render/FilamentRenderer.hpp — Teia cósmica FoF (Regime 8 apenas).
//
// Regra 0.1: retorna imediatamente se regime != REGIME_STRUCTURE.
// Regra 0.4: FoF nunca roda em SAFE mode; cache de 1s em MEDIUM/HIGH.
// Regra 0.5: zero alocação em Render().

#include "ICosmicRenderer.hpp"
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class FilamentRenderer : public ICosmicRenderer {
public:
    FilamentRenderer() = default;
    ~FilamentRenderer() override { Shutdown(); }

    bool Init(QualityTier quality) override;
    void OnQualityChanged(QualityTier new_quality) override;

    void Render(const ParticlePool& particles,
                int regime,
                const Camera& cam,
                float sim_time_myr) override;

    void Shutdown() override;

    /// Chamar no loop de update — não no Render (Regra 0.5).
    /// Respeita cooldown de 1s e SAFE mode (Regra 0.4).
    void Update(float delta_time, const ParticlePool& particles, int regime);

    void SetScreenSize(int w, int h) { screen_w_ = w; screen_h_ = h; }

    /// Forçar recálculo imediato (ex: mudança de regime, ajuste de linking_length).
    void InvalidateCache() { fof_cooldown_ = 0.0f; }

    float linking_length = 0.5f;  // exposto para ajuste via ImGui

    // Modo de preview: gera dados sintéticos para visualizar shaders
    void SetPreviewMode(bool enable);
    void GeneratePreviewData(int seed = 0, int complexity_scale = 1);

private:
    struct HaloNode {
        glm::vec3 center;
        float     mass;
    };

    struct FilamentEdge {
        int   a;           // índice halo A
        int   b;           // índice halo B
        float mass_total;
    };

    struct Config {
        int   max_halos;
        int   segments_per_edge;  // pontos ao longo do filamento
        int   max_edges;
    };
    static constexpr Config CONFIGS[3] = {
        {  0,  0,    0 },   // SAFE — FoF desabilitado, usar halos estáticos
        { 60,  8, 1000 },   // MEDIUM
        {150, 16, 3000 },   // HIGH
    };

    void RunFoF(const ParticlePool& particles);
    void RebuildVBO();

    static GLuint CompileShader(GLenum type, const std::string& path);
    static bool   LinkProgram(GLuint prog, GLuint vs, GLuint fs);

    GLuint vao_        = 0;
    GLuint vbo_pos_a_  = 0;  // vec3 pos_a
    GLuint vbo_pos_b_  = 0;  // vec3 pos_b
    GLuint vbo_mass_   = 0;  // float mass_total
    GLuint vbo_t_      = 0;  // float t (parâmetro ao longo do filamento)
    GLuint prog_       = 0;

    GLint uloc_mvp_  = -1;
    GLint uloc_time_ = -1;

    // Pré-alocados em Init
    std::vector<float> buf_pos_a_;
    std::vector<float> buf_pos_b_;
    std::vector<float> buf_mass_;
    std::vector<float> buf_t_;

    // Resultado do FoF — pré-alocado
    std::vector<HaloNode>    halos_;
    std::vector<FilamentEdge> edges_;

    Config  config_       = CONFIGS[1];
    int     vertex_count_ = 0;
    float   fof_cooldown_ = 0.0f;
    int     screen_w_     = 1280;
    int     screen_h_     = 720;
    bool    initialized_  = false;
    bool    vbo_dirty_    = true;
    // Preview state
    bool    preview_mode_      = false;
    int     preview_seed_      = 0;
    int     preview_complexity_ = 1;
};
