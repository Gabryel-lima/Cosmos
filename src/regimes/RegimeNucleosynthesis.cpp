// src/regimes/RegimeNucleosynthesis.cpp — Regime 2: Nucleossíntese do Big Bang (BBN)
#include "RegimeNucleosynthesis.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/Universe.hpp"
#include "../render/Renderer.hpp"
#include "../physics/NuclearNetwork.hpp"
#include "../physics/FluidGrid.hpp"
#include "../physics/Constants.hpp"
#include "../physics/ParticlePool.hpp"
#include <cmath>
#include <algorithm>

static NuclearNetwork bbn_network;

void RegimeNucleosynthesis::onEnter(Universe& state) {
    // Inicializa razão p/n a partir do equilíbrio em T=150 keV
    state.abundances = NuclearNetwork::equilibriumAbundances(state.temperature_keV);
    total_baryon_density_ = FluidGrid::baryonDensity(state.scale_factor);

    // Sincroniza pool de partículas: converte prótons/nêtrons do Regime 1
    // Partículas já definidas por RegimeManager::buildInitialState
}

void RegimeNucleosynthesis::onExit() {}

void RegimeNucleosynthesis::update(double cosmic_dt, double scale_factor, double temp_keV,
                                    Universe& universe)
{
    constexpr double regime_duration = CosmicClock::REGIME_START_TIMES[3] - CosmicClock::REGIME_START_TIMES[2];
    double progress_dt = (regime_duration > 0.0) ? cosmic_dt / regime_duration : 0.0;
    double visual_dt = cosmic_dt <= 0.0 ? 0.0
                                         : std::clamp(progress_dt * 24.0, 0.001, 0.04);

    // Avança a rede de reações nucleares
    bbn_network.step(universe.abundances, cosmic_dt, temp_keV, scale_factor);

    // Atualiza as cores visuais das partículas com base nas abundâncias
    // Reatribui tipos ao longo do tempo conforme He4 se forma
    ParticlePool& p = universe.particles;
    size_t n = p.x.size();
    if (n > 0 && n < 20000) {
        double total = universe.abundances.Xp + universe.abundances.Xn
                     + universe.abundances.Xd + universe.abundances.Xhe4;
        if (total > 0.0) {
            // Fracções para cada tipo
            double fp  = universe.abundances.Xp  / total;
            double fn  = universe.abundances.Xn  / total;
            double fd  = universe.abundances.Xd  / total;
            // He4 = resto
            size_t active_count = 0;
            for (size_t i = 0; i < n; ++i)
                if (p.flags[i] & PF_ACTIVE) ++active_count;

            size_t fi = 0;
            for (size_t i = 0; i < n; ++i) {
                if (!(p.flags[i] & PF_ACTIVE)) continue;
                double frac = static_cast<double>(fi) / static_cast<double>(std::max<size_t>(1, active_count));
                ++fi;
                ParticleType new_type;
                if      (frac < fp)        new_type = ParticleType::PROTON;
                else if (frac < fp + fn)   new_type = ParticleType::NEUTRON;
                else if (frac < fp+fn+fd)  new_type = ParticleType::DEUTERIUM;
                else                       new_type = ParticleType::HELIUM4NUCLEI;
                p.type[i] = new_type;
                ParticlePool::defaultColor(new_type, p.color_r[i], p.color_g[i], p.color_b[i]);
                // He4 é mais brilhante (recém formado)
                p.luminosity[i] = (new_type == ParticleType::HELIUM4NUCLEI) ? 2.0f : 1.0f;

                if (visual_dt > 0.0) {
                    double phase = universe.cosmic_time * 0.05 + static_cast<double>(i) * 0.13;
                    double jitter = (new_type == ParticleType::HELIUM4NUCLEI) ? 0.006 : 0.003;
                    p.vx[i] = p.vx[i] * 0.985 + std::sin(phase) * jitter;
                    p.vy[i] = p.vy[i] * 0.985 + std::cos(phase * 1.1) * jitter;
                    p.vz[i] = p.vz[i] * 0.985 + std::sin(phase * 0.7) * jitter;

                    double collapse = (new_type == ParticleType::HELIUM4NUCLEI || new_type == ParticleType::DEUTERIUM) ? 0.02 : 0.008;
                    p.vx[i] += -p.x[i] * collapse * visual_dt;
                    p.vy[i] += -p.y[i] * collapse * visual_dt;
                    p.vz[i] += -p.z[i] * collapse * visual_dt;
                    p.x[i] += p.vx[i] * visual_dt;
                    p.y[i] += p.vy[i] * visual_dt;
                    p.z[i] += p.vz[i] * visual_dt;
                }
            }
        }
    }

    // Expande posições com o universo
    // (tratado no tick do RegimeManager via razão do fator de escala)
    universe.scale_factor    = scale_factor;
    universe.temperature_keV = temp_keV;
}

void RegimeNucleosynthesis::render(Renderer& renderer, const Universe& universe) {
    renderer.renderParticles(universe);
    renderer.renderNuclearAbundances(universe.abundances);
}
