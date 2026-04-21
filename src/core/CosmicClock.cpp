// src/core/CosmicClock.cpp
#include "CosmicClock.hpp"
#include "../physics/Friedmann.hpp"
#include "../physics/Constants.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>

CosmicClock::CosmicClock() {
    cosmic_time_    = REGIME_START_TIMES[0];
    scale_factor_   = phys::scale_at_temperature_keV(T_INFLATION_END * 10.0);
    if (scale_factor_ <= 0.0 || !std::isfinite(scale_factor_))
        scale_factor_ = 1e-28;
    recomputeDerivedQuantities();
    updateRegimeIndex();
}

void CosmicClock::play() {
    paused_ = false;
}

void CosmicClock::pause() {
    paused_ = true;
    single_frame_step_ = false;
}

void CosmicClock::stepSingleFrame() {
    single_frame_step_ = true;
    paused_ = false;
}

void CosmicClock::step(double real_dt_seconds) {
    if (paused_ && !single_frame_step_) return;

    double cosmic_dt = real_dt_seconds * time_scale_;

    // Sub-passos adaptativos internos para manter o RK4 estável
    // Em escalas de tempo extremas, não devemos ultrapassar um limite de regime
    constexpr int MAX_SUBSTEPS = 16;
    double remaining = cosmic_dt;
    for (int sub = 0; sub < MAX_SUBSTEPS && remaining > 0.0; ++sub) {
        double sub_dt = remaining / static_cast<double>(MAX_SUBSTEPS - sub);
        scale_factor_ = phys::integrate_scale_factor(scale_factor_, sub_dt);
        if (scale_factor_ <= 0.0 || !std::isfinite(scale_factor_))
            scale_factor_ = 1e-60;
        cosmic_time_ += sub_dt;
        remaining -= sub_dt;
    }

    recomputeDerivedQuantities();
    updateRegimeIndex();

    // Limitar ao tempo atual do universo
    if (cosmic_time_ > phys::t_today) {
        cosmic_time_ = phys::t_today;
        scale_factor_ = 1.0;
        paused_ = true;
    }

    if (single_frame_step_) {
        single_frame_step_ = false;
        paused_ = true;
    }
}

void CosmicClock::setTimeScale(double scale) {
    time_scale_ = std::max(1e-50, scale);
    std::printf("[TIME] Scale set to %.4e (regime %d)\n", time_scale_, regime_index_);
}

void CosmicClock::applySpeedPreset(SpeedPreset preset) {
    double base = DEFAULT_SCALE[static_cast<size_t>(regime_index_)];
    switch (preset) {
        case SpeedPreset::SLOW_MOTION:        setTimeScale(base * 0.01); break;
        case SpeedPreset::NORMAL:             setTimeScale(base);        break;
        case SpeedPreset::FAST_FORWARD_2X:    setTimeScale(base * 2.0);  break;
        case SpeedPreset::FAST_FORWARD_10X:   setTimeScale(base * 10.0); break;
        case SpeedPreset::FAST_FORWARD_100X:  setTimeScale(base * 100.0); break;
    }
}

void CosmicClock::applyRegimeDefaultScale(int regime_index) {
    int idx = std::clamp(regime_index, 0, 4);
    setTimeScale(DEFAULT_SCALE[static_cast<size_t>(idx)]);
}

void CosmicClock::jumpToCosmicTime(double t_seconds) {
    cosmic_time_ = std::max(0.0, std::min(t_seconds, phys::t_today));
    // Reconstruir a(t) do zero usando integração de Friedmann
    // Para um salto, usamos a aproximação analítica T(a) = T_CMB/a,
    // depois recalculamos a a partir do tempo cósmico desejado numericamente.
    // Para tempos iniciais: dominado por radiação: t ≈ 1/(2H) => a ∝ t^(1/2)
    // Para dominado por matéria: a ∝ t^(2/3)
    // Por simplicidade, itera via bisseção em cosmic_time_from_scale
    if (cosmic_time_ >= phys::t_today) {
        scale_factor_ = 1.0;
    } else {
        // Bisseção: encontrar a tal que cosmic_time_from_scale(a) ≈ cosmic_time_
        double a_lo = 1e-40, a_hi = 2.0;
        for (int iter = 0; iter < 60; ++iter) {
            double a_mid = std::sqrt(a_lo * a_hi); // ponto médio geométrico
            double t_mid = phys::cosmic_time_from_scale(a_mid);
            if (t_mid < cosmic_time_) a_lo = a_mid;
            else                      a_hi = a_mid;
        }
        scale_factor_ = std::sqrt(a_lo * a_hi);
    }
    recomputeDerivedQuantities();
    updateRegimeIndex();
}

void CosmicClock::jumpToRegime(int regime_index) {
    int idx = std::clamp(regime_index, 0, 4);
    jumpToCosmicTime(REGIME_START_TIMES[static_cast<size_t>(idx)]);
    applyRegimeDefaultScale(idx);
    std::printf("[REGIME] Jumped to regime %d at t=%.4e s, T=%.4e keV\n",
                idx, cosmic_time_, temperature_keV_);
}

void CosmicClock::jumpToEpochFraction(float f) {
    f = std::clamp(f, 0.0f, 1.0f);
    // Mapear f ∈ [0,1] para tempo cósmico em escala logarítmica (10⁻⁴³ a 4,35×10¹⁷)
    double log_min = std::log10(1e-43);
    double log_max = std::log10(phys::t_today);
    double log_t = log_min + f * (log_max - log_min);
    jumpToCosmicTime(std::pow(10.0, log_t));
}

void CosmicClock::initializeToDefaultState() {
    jumpToRegime(0);  // Iniciar na inflação
    paused_ = true;   // Aguardar o usuário pressionar Reproduzir
    std::printf("[TIME] Initialized to default state: regime 0, inflation\n");
}

double CosmicClock::getHubbleRate() const {
    return phys::hubble_from_scale(scale_factor_);
}

double CosmicClock::getRegimeProgress() const {
    int r = regime_index_;
    double t_start = REGIME_START_TIMES[static_cast<size_t>(r)];
    double t_end   = (r < 4) ? REGIME_START_TIMES[static_cast<size_t>(r + 1)] : phys::t_today;
    if (t_end <= t_start) return 1.0;
    return std::clamp((cosmic_time_ - t_start) / (t_end - t_start), 0.0, 1.0);
}

void CosmicClock::recomputeDerivedQuantities() {
    temperature_keV_ = phys::temperature_keV_from_scale(scale_factor_);
}

void CosmicClock::updateRegimeIndex() {
    int new_regime;
    if (temperature_keV_ > T_INFLATION_END)
        new_regime = 0;
    else if (temperature_keV_ > T_QGP_END)
        new_regime = 1;
    else if (temperature_keV_ > T_BBN_END)
        new_regime = 2;
    else if (temperature_keV_ > T_RECOMBINATION)
        new_regime = 3;
    else
        new_regime = 4;

    if (new_regime != regime_index_) {
        std::printf("[REGIME] Transitioning %d→%d at T=%.4e keV, t=%.4e s\n",
                    regime_index_, new_regime, temperature_keV_, cosmic_time_);
        regime_index_ = new_regime;
    }
}
