#pragma once
// src/render/StarFormationFX.hpp — Efeito visual de colapso/formação estelar (Regimes 7–8).
//
// Máquina de estados por evento (não por partícula física):
//   COLLAPSING  → PROTO_STAR → STAR_BORN
//
// CONTRATO:
//   - Update() modifica visual_offset_x/y/z das partículas de gás — NUNCA position/velocity.
//   - Render() é zero-alocação (Regra 0.5).
//   - Guarda regime >= REGIME_REIONIZATION (Regra 0.1).

#include "ICosmicRenderer.hpp"
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>

class StarFormationFX : public ICosmicRenderer {
public:
    enum class EventState : uint8_t {
        DETECTING  = 0,
        COLLAPSING,
        PROTO_STAR,
        STAR_BORN,
    };

    struct FormationEvent {
        glm::vec3  center          = {};
        float      elapsed         = 0.0f;
        float      glow_radius     = 0.0f;
        float      final_radius    = 1.0f;
        float      influence_radius= 2.0f;
        EventState state           = EventState::COLLAPSING;
    };

    StarFormationFX() = default;
    ~StarFormationFX() override { Shutdown(); }

    bool Init(QualityTier quality) override;
    void OnQualityChanged(QualityTier new_quality) override;

    void Render(const ParticlePool& particles,
                int regime,
                const Camera& cam,
                float sim_time_myr) override;

    void Shutdown() override;

    /// Chamar no loop de update — não no render.
    /// Avança a FSM e atualiza visual_offset das partículas de gás próximas.
    /// NUNCA modifica position ou velocity (Regra 0.3).
    void Update(float delta_time, ParticlePool& particles, int regime);

    /// Registrar um novo evento de colapso (chamado pela lógica de física).
    void AddEvent(const FormationEvent& ev);

    void SetScreenSize(int w, int h) { screen_w_ = w; screen_h_ = h; }

private:
    struct Config {
        float t_collapse;   // segundos reais até PROTO_STAR
        float t_ignition;   // segundos reais até STAR_BORN
        int   max_events;
    };
    static constexpr Config CONFIGS[3] = {
        { 0.5f, 0.5f, 10  },  // SAFE
        { 1.5f, 2.0f, 30  },  // MEDIUM
        { 2.5f, 3.0f, 60  },  // HIGH
    };

    void UploadEvents();

    static GLuint CompileShader(GLenum type, const std::string& path);
    static bool   LinkProgram(GLuint prog, GLuint vs, GLuint fs);

    GLuint vao_     = 0;
    GLuint vbo_pos_ = 0;  // vec3 center
    GLuint vbo_rad_ = 0;  // float glow_radius
    GLuint vbo_pha_ = 0;  // float phase (COLLAPSING=0, PROTO=1, BORN=2)
    GLuint prog_    = 0;

    GLint uloc_view_   = -1;
    GLint uloc_proj_   = -1;
    GLint uloc_screen_ = -1;

    // Pré-alocados — zero alloc em Render
    std::vector<float> pos_buf_;
    std::vector<float> rad_buf_;
    std::vector<float> pha_buf_;

    // Eventos ativos — capacidade pré-alocada em Init
    std::vector<FormationEvent> events_;

    Config  config_    = CONFIGS[1];
    int     draw_count_= 0;
    int     screen_w_  = 1280;
    int     screen_h_  = 720;
    bool    initialized_ = false;
};
