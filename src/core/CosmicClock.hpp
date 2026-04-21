#pragma once
// src/core/CosmicClock.hpp — Controlador mestre do tempo e integrador de Friedmann.
// Única autoridade no tempo de simulação. Regimes NÃO rastreiam tempo absoluto.

#include <array>

class CosmicClock {
public:
    // ── Limiares de transição de regime [keV] ──────────────────────────────
    static constexpr double T_INFLATION_END = 1e16;   // regime 0→1
    static constexpr double T_QGP_END       = 150.0;  // regime 1→2
    static constexpr double T_BBN_END       = 0.07;   // regime 2→3
    static constexpr double T_RECOMBINATION = 2.6e-4; // regime 3→4 (0.26 eV)

    // ── Escalas de tempo padrão por regime (cosmic_dt / real_dt) ─────────────────
    // Ajustadas para que cada época seja completada em ~30 s reais.
    // Regime 0: inflação de t=1e-43 a t=1e-32 s  → Δt≈1e-32, 30s → 3.3e-34 s/s
    // Regime 1: PQG  de t=1e-32 a t=6e-5 s       → usa RD analítico; 2e-6 dá ~30 s
    // Regimes 2-4: idem antes.
    static constexpr std::array<double,5> DEFAULT_SCALE = {
        3.33e-34,   // Regime 0: Inflação      (~10⁻³² s de intervalo / 30 s reais)
        2.0e-6,     // Regime 1: PQG           (solução RD analítica; QGP em ~30 s)
        40.0,       // Regime 2: NBB           (~1200 s / 30 s)
        4.0e11,     // Regime 3: Plasma        (~1,2e13 s / 30 s)
        1.33e16,    // Regime 4: Estrutura     (~4e17 s / 30 s)
    };

    /// Multiplicadores de preset de velocidade relativos ao DEFAULT_SCALE.
    enum class SpeedPreset {
        SLOW_MOTION,        // ×0,01
        NORMAL,             // ×1,0  (padrão do regime)
        FAST_FORWARD_2X,    // ×2,0
        FAST_FORWARD_10X,   // ×10,0
        FAST_FORWARD_100X,  // ×100,0
    };

    CosmicClock();

    // ── Controle de reprodução ─────────────────────────────────────────────────
    void play();
    void pause();
    void stepSingleFrame();
    bool isPaused() const { return paused_; }

    // ── Avançar simulação por real_dt segundos ──────────────────────────
    // Chamado a cada quadro pelo loop principal. Respeita time_scale e estado pausado.
    void step(double real_dt_seconds);

    // ── Controle de velocidade ───────────────────────────────────────────────────
    void   setTimeScale(double scale);
    double getTimeScale() const { return time_scale_; }
    void   applySpeedPreset(SpeedPreset preset);
    void   applyRegimeDefaultScale(int regime_index);

    // ── Navegação (busca) ─────────────────────────────────────────────────
    void jumpToCosmicTime(double t_seconds);
    void jumpToRegime(int regime_index);
    void jumpToEpochFraction(float f);

    // ── Inicialização ───────────────────────────────────────────────
    // Chamada uma vez em main() após a construção. Garante que a cena nunca esteja vazia.
    void initializeToDefaultState();

    // ── Leituras ────────────────────────────────────────────────────────────
    double getCosmicTime()        const { return cosmic_time_; }
    double getScaleFactor()       const { return scale_factor_; }
    double getTemperatureKeV()    const { return temperature_keV_; }
    double getHubbleRate()        const;
    int    getCurrentRegimeIndex() const { return regime_index_; }
    double getRegimeProgress()    const;

    // ── Tempos de início dos regimes (segundos desde o Big Bang) ───────────────────────
    static constexpr std::array<double,5> REGIME_START_TIMES = {
        1e-43,   // Regime 0: Tempo de Planck
        1e-35,   // Regime 1: após inflação
        1e-6,    // Regime 2: após PQG
        1200.0,  // Regime 3: após NBB
        1.2e13,  // Regime 4: após recombinação
    };

private:
    void recomputeDerivedQuantities();
    void updateRegimeIndex();

    double cosmic_time_     = 1e-43;  // seconds since Big Bang
    double scale_factor_    = 1e-28;  // a(t) — very small at Planck time
    double temperature_keV_ = 1e28;   // T in keV
    double time_scale_      = DEFAULT_SCALE[0];
    bool   paused_          = true;   // start paused until initialized
    int    regime_index_    = 0;

    bool   single_frame_step_ = false; // for stepSingleFrame()
};
