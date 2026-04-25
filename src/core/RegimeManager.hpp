#pragma once
// src/core/RegimeManager.hpp — Motor automático de transição de regime.
//
// CONTRATO: RegimeManager controla TODAS as transições de regime automaticamente.
// Código externo (loop principal, UI) NUNCA chama lógica de transição privada diretamente.
// A única interface pública para navegação é jumpToRegime() (atalho de busca).
// Regimes são detalhes de implementação — não estados visíveis ao usuário.

#include "../core/Universe.hpp"
#include "CosmicClock.hpp"
#include "../regimes/IRegime.hpp"
#include <array>
#include <memory>
#include <string>

class CosmicClock;
class Renderer;

struct InitialState {
    double            cosmic_time      = 0.0;
    double            scale_factor     = 1e-28;
    double            temperature_keV  = 1e28;
    ParticlePool      particles;
    GridData          field;
    NuclearAbundances abundances;
    CameraState       suggested_camera;
};

class RegimeManager {
public:
    RegimeManager();

    /// Chamado a cada quadro pelo loop principal.
    /// Lê a temperatura do relógio, aciona transição automática quando o limiar é cruzado.
    void tick(CosmicClock& clock, Universe& universe, double real_dt_seconds);

    /// Atalho de navegação — reconstrói estado para aquele regime e retoma a evolução.
    void jumpToRegime(int index, CosmicClock& clock, Universe& universe);

    /// Constrói um estado inicial fisicamente consistente para o tempo inicial de qualquer regime.
    InitialState buildInitialState(int regime_index);

    /// Renderiza o regime atual (e interpola durante transições).
    void render(Renderer& renderer, const Universe& universe);

    IRegime* getCurrentRegime() const;
    int      getCurrentRegimeIndex() const { return active_index_; }

    /// Retorna progresso [0,1] de mistura durante transição (0 = sem transição).
    float getTransitionProgress() const { return in_transition_ ? transition_t_ : 0.0f; }

private:
    void checkAndTransition(CosmicClock& clock, Universe& universe);
    void beginTransition(int from, int to, Universe& universe, CosmicClock& clock);
    void applyInitialState(int regime_index, InitialState& state, Universe& universe);
    void renderRegime(int regime_index, Renderer& renderer, const Universe& universe) const;

    std::array<std::unique_ptr<IRegime>, CosmicClock::REGIME_COUNT> regimes_;
    int   active_index_   = 0;
    bool  in_transition_  = false;
    float transition_t_   = 0.0f;   // progresso de mistura 0..1
    float transition_dur_ = 2.0f;   // segundos reais para transição
    int   transition_from_ = 0;
    int   transition_to_   = 1;

    double last_real_time_ = 0.0;   // para o temporizador de transição (definido em tick)
    float  transition_elapsed_ = 0.0f;
    float  regime_elapsed_real_ = 0.0f;   // tempo real acumulado no regime ativo
    // Permanência mínima por regime antes de permitir a próxima transição automática.
    // Evita pular rápido demais sem dar tempo de aparecer visualmente.
    std::array<float, CosmicClock::REGIME_COUNT> min_regime_dwell_s_ = {0.0f, 2.0f, 1.0f, 1.0f, 1.2f, 1.2f, 0.0f};
    double speed_mult_at_start_ = 1.0; // multiplicador de velocidade capturado ao início da transição
    Universe transition_from_universe_;
};
