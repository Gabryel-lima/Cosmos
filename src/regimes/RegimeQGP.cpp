// src/regimes/RegimeQGP.cpp — Regime 1: Plasma Quark-Glúon
#include "RegimeQGP.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/Universe.hpp"
#include "../render/Renderer.hpp"
#include "../physics/Constants.hpp"
#include "../physics/Hadronization.hpp"
#include "../physics/ParticlePool.hpp"
#include <random>
#include <cmath>
#include <algorithm>

static std::mt19937 rng_qgp(7);

void RegimeQGP::onEnter(Universe& state) {
    prev_scale_factor_ = state.scale_factor;
    hadronized_        = false;
    // Partículas já posicionadas por RegimeManager::buildInitialState()
}

void RegimeQGP::onExit() {}

// ── Interações Yukawa em unidades de simulação ─────────────────────────────────────
// Modelo puramente visual: força = ±exp(-r/λ)/r² em unidades da caixa.
// λ aumenta conforme T cai (confinamento cresce), tornando as interações de maior alcance.

void RegimeQGP::applyYukawaForces(Universe& universe, double temp_keV, double dt) {
    ParticlePool& p = universe.particles;
    size_t n = p.x.size();
    if (n == 0) return;

    // Comprimento de triagem em unidades de sim: cresce à medida que T cai para T_QGP_END
    // Em T=1e16 keV: lam=0.03 (curto alcance, plasma quente). Em T=150 keV: lam=0.2
    double t_ratio = std::max(temp_keV, 150.0) / 1e16;  // 1 → 150/1e16
    double lam = 0.03 + 0.17 * (1.0 - t_ratio);  // 0.03 → 0.20

    // Intensidade escalada para dar velocidades visíveis (~0.001 sim/frame)
    double strength = 0.002 * dt;

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        double fx = 0, fy = 0, fz = 0;
        for (size_t j = i + 1; j < n; ++j) {
            if (!(p.flags[j] & PF_ACTIVE)) continue;
            double dx = p.x[j] - p.x[i];
            double dy = p.y[j] - p.y[i];
            double dz = p.z[j] - p.z[i];
            double r2 = dx*dx + dy*dy + dz*dz;
            if (r2 < 1e-10 || r2 > (lam * 5.0) * (lam * 5.0)) continue;
            double r = std::sqrt(r2);
            double mag = std::exp(-r / lam) / r2;
            // Carga de cor: mesma carga → repulsão, oposta → atração
            float qi = p.charge[i], qj = p.charge[j];
            double sign = (qi * qj > 0.0f) ? 1.0 : -1.0;
            double f = sign * mag;
            fx += f * dx;  fy += f * dy;  fz += f * dz;
            // 3ª lei de Newton
            p.vx[j] -= f * dx * strength;
            p.vy[j] -= f * dy * strength;
            p.vz[j] -= f * dz * strength;
        }
        p.vx[i] += fx * strength;
        p.vy[i] += fy * strength;
        p.vz[i] += fz * strength;
    }

    // Integrar posições
    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        p.x[i] += p.vx[i] * dt;
        p.y[i] += p.vy[i] * dt;
        p.z[i] += p.vz[i] * dt;
    }
}

void RegimeQGP::applyCosmicExpansion(Universe& universe, double a_prev, double a_new) {
    if (a_prev <= 0.0) return;
    double ratio = a_new / a_prev;
    ParticlePool& p = universe.particles;
    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        // Não multiplicar p.x[i] *= ratio geométricamente! 
        // Isso causa que as coordenadas explodam por 1.000.000x, 
        // distanciando os objetos enormemente e forçando a câmera 
        // a se afastar tanto que eles somem da tela.
        // Manter em coordenadas comóveis na visualização.
        
        // Velocidades peculiares diminuem: v ∝ 1/a
        p.vx[i] /= ratio;
        p.vy[i] /= ratio;
        p.vz[i] /= ratio;
    }
}

void RegimeQGP::hadronize(Universe& universe) {
    if (hadronized_) return;
    hadronized_ = true;
    chemistry::hadronizeQgp(universe.particles);
}

void RegimeQGP::update(double cosmic_dt, double scale_factor, double temp_keV,
                        Universe& universe)
{
    double a_new = scale_factor;
    constexpr double regime_duration = CosmicClock::REGIME_START_TIMES[2] - CosmicClock::REGIME_START_TIMES[1];
    double progress_dt = (regime_duration > 0.0) ? cosmic_dt / regime_duration : 0.0;
    double visual_dt = cosmic_dt <= 0.0 ? 0.0
                                         : std::clamp(progress_dt * 20.0, 0.001, 0.05);

    // Yukawa O(N²): visualmente seguro para N ≤ 10 000
    if (universe.particles.x.size() <= 10000 && visual_dt > 0.0) {
        applyYukawaForces(universe, temp_keV, visual_dt);
    }

    applyCosmicExpansion(universe, prev_scale_factor_, a_new);
    prev_scale_factor_ = a_new;

    if (temp_keV < 150.0 && !hadronized_) {
        hadronize(universe);
    }

    universe.scale_factor    = scale_factor;
    universe.temperature_keV = temp_keV;
}

void RegimeQGP::render(Renderer& renderer, const Universe& universe) {
    renderer.renderParticles(universe);
}
