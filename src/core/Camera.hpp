#pragma once
// src/core/Camera.hpp — Câmera 3D 6DOF com modo LOD guiado por zoom.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>

struct InputState {
    bool w, a, s, d, q, e;   // teclas de voo
    float mouse_dx, mouse_dy; // delta do mouse neste quadro
    float scroll_delta;       // rolagem da roda
    bool tab_pressed;         // ciclar modo
    bool t_pressed;           // rastrear o mais próximo
    bool esc_pressed;         // liberar rastreamento
    bool c_pressed;           // ir para posição padrão
};

class Camera {
public:
    enum class Mode { COSMIC, GALACTIC, STELLAR, ATOMIC };

    // Limiares de LOD em Mpc comovente
    static constexpr double DIST_COSMIC   = 100.0;
    static constexpr double DIST_GALACTIC = 0.1;
    static constexpr double DIST_STELLAR  = 1e-7;
    static constexpr double DIST_ATOMIC   = 1e-28;

    // Transformação no espaço do mundo (dupla precisão para grandes escalas)
    glm::dvec3 position  = { 0.0, 0.0, 5.0 };
    glm::vec3  forward   = { 0.0f, 0.0f, -1.0f };
    glm::vec3  up        = { 0.0f, 1.0f,  0.0f };
    float      fov_deg   = 60.0f;
    double     zoom_distance = DIST_COSMIC;
    Mode       mode      = Mode::COSMIC;
    uint32_t   tracked_id = 0;  // 0 = câmera livre

    // Modo ortográfico (Regime 0 Fase A)
    bool   ortho_mode = false;
    float  ortho_size = 1.0f;  // semi-extensão em unidades do mundo

    Camera() = default;

    // ── Controles ─────────────────────────────────────────────────────────
    void processMouseDelta(float dx, float dy);
    void processScroll(float delta);
    void processKeyboard(const InputState& in, float dt);
    void trackParticle(uint32_t particle_id);
    void releaseTracking();

    // ── Atualizar posição da partícula rastreada ─────────────────────────────────
    void updateTracking(glm::dvec3 target_world_pos);

    // ── Gerenciamento de LOD ──────────────────────────────────────────────
    void updateMode();  // chamado após processScroll

    // ── Matrizes ──────────────────────────────────────────────────────────
    glm::mat4 getViewMatrix()              const;
    glm::mat4 getProjectionMatrix(float aspect) const;

    // ── Ir para um estado de câmera sugerido para um regime ────────────────────
    struct State {
        glm::dvec3 position;
        glm::vec3  forward;
        double     zoom_distance;
        bool       ortho_mode;
    };
    void applyState(const State& s);
    State getRegimeDefaultState(int regime_index) const;

private:
    float yaw_    = -90.0f; // graus, olhando para -Z
    float pitch_  =   0.0f; // graus
    float move_speed_ = 0.5f; // unidades do mundo por segundo (auto-escalado com zoom)

    glm::dvec3 look_at_target_ = { 0.0, 0.0, 0.0 };
    bool       orbiting_ = false; // true quando rastreando uma partícula

    void rebuildForwardFromAngles();
};
