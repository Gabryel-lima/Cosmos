// src/regimes/RegimeNucleosynthesis.cpp — Regime 2: Nucleossíntese do Big Bang (BBN)
#include "RegimeNucleosynthesis.hpp"
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
    // Avança a rede de reações nucleares
    bbn_network.step(universe.abundances, cosmic_dt, temp_keV, scale_factor);

    // Atualiza as cores visuais das partículas com base nas abundâncias
    // (Para N < 10.000 partículas explícitas, repinta; caso contrário usa agregado)
    ParticlePool& p = universe.particles;
    size_t n = p.x.size();
    if (n > 0 && n < 10000) {
        // Usa frações de abundância para reatribuir tipos aleatoriamente
        // (simplificado: atribuição probabilística)
        double total = universe.abundances.Xp + universe.abundances.Xn
                     + universe.abundances.Xd + universe.abundances.Xhe4;
        if (total > 0.0) {
            for (size_t i = 0; i < n; ++i) {
                if (!(p.flags[i] & PF_ACTIVE)) continue;
                // Mantém o tipo existente, apenas atualiza as cores
                ParticlePool::defaultColor(p.type[i],
                    p.color_r[i], p.color_g[i], p.color_b[i]);
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
