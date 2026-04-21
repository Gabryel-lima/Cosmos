// src/core/RegimeManager.cpp
#include "RegimeManager.hpp"
#include "CosmicClock.hpp"
#include "../regimes/RegimeInflation.hpp"
#include "../regimes/RegimeQGP.hpp"
#include "../regimes/RegimeNucleosynthesis.hpp"
#include "../regimes/RegimePlasma.hpp"
#include "../regimes/RegimeStructure.hpp"
#include "../render/Renderer.hpp"
#include "../physics/Friedmann.hpp"
#include "../physics/Constants.hpp"
#include <random>
#include <cmath>
#include <cstdio>
#include <algorithm>

// ── Construção ─────────────────────────────────────────────────────────────

RegimeManager::RegimeManager() {
    regimes_[0] = std::make_unique<RegimeInflation>();
    regimes_[1] = std::make_unique<RegimeQGP>();
    regimes_[2] = std::make_unique<RegimeNucleosynthesis>();
    regimes_[3] = std::make_unique<RegimePlasma>();
    regimes_[4] = std::make_unique<RegimeStructure>();
}

// ── Construtores de Estado Inicial ─────────────────────────────────────────────────────

static std::mt19937 rng_init(9999);

/// Aproximação de Zel'dovich: desloca grade regular com perturbações de densidade.
/// Produz distribuição inicial de partículas cosmologicamente motivada em z~20.
static void zelDovichDisplace(ParticlePool& pool, int N_cbrt, double box_size) {
    std::normal_distribution<double> gauss(0.0, 0.05 * box_size);
    int N = N_cbrt * N_cbrt * N_cbrt;
    pool.clear();
    double spacing = box_size / static_cast<double>(N_cbrt);
    for (int k = 0; k < N_cbrt; ++k)
    for (int j = 0; j < N_cbrt; ++j)
    for (int i = 0; i < N_cbrt; ++i) {
        double x = (i + 0.5) * spacing + gauss(rng_init);
        double y = (j + 0.5) * spacing + gauss(rng_init);
        double z = (k + 0.5) * spacing + gauss(rng_init);
        float cr, cg, cb;
        // 80% matéria escura, 20% gás
        ParticleType t = (rng_init() % 5 == 0) ? ParticleType::GAS : ParticleType::DARK_MATTER;
        ParticlePool::defaultColor(t, cr, cg, cb);
        pool.add(x, y, z, 0.0, 0.0, 0.0, phys::m_p * 1e50, t, cr, cg, cb);
    }
    (void)N;
}

InitialState RegimeManager::buildInitialState(int regime_index) {
    InitialState st;
    int idx = std::clamp(regime_index, 0, 4);
    st.cosmic_time     = CosmicClock::REGIME_START_TIMES[static_cast<size_t>(idx)];
    st.scale_factor    = phys::scale_at_temperature_keV(
        [&]() -> double {
            switch (idx) {
                case 0: return 1e20;   // inflação profunda
                case 1: return CosmicClock::T_INFLATION_END;
                case 2: return CosmicClock::T_QGP_END;
                case 3: return CosmicClock::T_BBN_END;
                case 4: return CosmicClock::T_RECOMBINATION;
                default: return 1.0;
            }
        }());
    if (st.scale_factor <= 0.0 || !std::isfinite(st.scale_factor))
        st.scale_factor = 1e-28;
    st.temperature_keV = phys::temperature_keV_from_scale(st.scale_factor);

    // Sugestões de câmera
    CameraState cam;
    switch (idx) {
        case 0: cam.ortho = true;  cam.zoom = 1.0; break;
        case 1: cam.ortho = false; cam.zoom = 3.0; break;
        case 2: cam.ortho = false; cam.zoom = 3.0; break;
        case 3: cam.ortho = false; cam.zoom = 100.0; break;
        case 4: cam.ortho = false; cam.zoom = 500.0; break;
    }
    cam.pos_z = static_cast<double>(cam.zoom) * 1.5;
    st.suggested_camera = cam;

    // Preencher partículas ────────────────────────────────────────────────────
    switch (idx) {
        case 0:
            // Inflação: sem partículas; campo escalar inicializado por RegimeInflation::onEnter
            break;

        case 1: {
            // PQG: N quarks amostrados de distribuição Gaussiana (sem campo de densidade ainda)
            int N = 50000;
            std::uniform_real_distribution<double> pos_dist(-0.5, 0.5);
            std::normal_distribution<double> vel_dist(0.0, 0.1);
            static const ParticleType quark_types[3] = {
                ParticleType::QUARK_U, ParticleType::QUARK_D, ParticleType::QUARK_S
            };
            for (int i = 0; i < N; ++i) {
                double px = pos_dist(rng_init), py = pos_dist(rng_init), pz = pos_dist(rng_init);
                double vx = vel_dist(rng_init), vy = vel_dist(rng_init), vz = vel_dist(rng_init);
                ParticleType t = quark_types[rng_init() % 3];
                float cr, cg, cb; ParticlePool::defaultColor(t, cr, cg, cb);
                st.particles.add(px, py, pz, vx, vy, vz, phys::m_p / 3.0, t, cr, cg, cb,
                                 (t == ParticleType::QUARK_U) ? 2.0f/3.0f : -1.0f/3.0f);
            }
            // Também adicionar glúons
            for (int i = 0; i < N / 5; ++i) {
                double px = pos_dist(rng_init), py = pos_dist(rng_init), pz = pos_dist(rng_init);
                st.particles.add(px, py, pz,
                                 vel_dist(rng_init), vel_dist(rng_init), vel_dist(rng_init),
                                 1e-40, ParticleType::GLUON, 1.0f, 1.0f, 1.0f, 0.0f);
            }
            break;
        }

        case 2: {
            // NBB: prótons e nêutrons (da hadronização)
            st.abundances = NuclearAbundances{};
            st.abundances.Xp = 0.875; st.abundances.Xn = 0.125;
            int N = 10000;
            std::uniform_real_distribution<double> pos_dist(-0.5, 0.5);
            std::normal_distribution<double> vel_dist(0.0, 0.01);
            for (int i = 0; i < N; ++i) {
                bool is_proton = (rng_init() % 8 != 0);
                ParticleType t = is_proton ? ParticleType::PROTON : ParticleType::NEUTRON;
                float cr, cg, cb; ParticlePool::defaultColor(t, cr, cg, cb);
                st.particles.add(pos_dist(rng_init), pos_dist(rng_init), pos_dist(rng_init),
                                 vel_dist(rng_init), vel_dist(rng_init), vel_dist(rng_init),
                                 phys::m_p, t, cr, cg, cb);
            }
            break;
        }

        case 3: {
            // Plasma: inicializar grade de fluido, mínimo de partículas explícitas
            int N = 64;
            st.field.resize(N, N, N);
            // Perturbações semente BAO: Gaussiana isotrópica com amplitude δ ~ 10⁻⁵
            std::normal_distribution<float> delta_noise(0.0f, 1e-5f);
            for (float& v : st.field.data) v = delta_noise(rng_init);
            break;
        }

        case 4: {
            // Estrutura: grade de Zel'dovich em z~20
            // N_cbrt³ partículas
            int N_cbrt = 46;  // ~100k partículas
            double box = 100.0;  // Mpc comóvel
            zelDovichDisplace(st.particles, N_cbrt, box);
            // Campo de densidade semente do Regime 3
            st.field.resize(64, 64, 64);
            std::normal_distribution<float> dn(0.0f, 0.01f);
            for (float& v : st.field.data) v = dn(rng_init);
            break;
        }
    }
    return st;
}

// ── Aplicar estado inicial ao Universo ──────────────────────────────────────────────

void RegimeManager::applyInitialState(int regime_index, InitialState& state,
                                       Universe& universe)
{
    universe.particles      = std::move(state.particles);
    universe.abundances     = state.abundances;
    universe.scale_factor   = state.scale_factor;
    universe.temperature_keV= state.temperature_keV;
    universe.cosmic_time    = state.cosmic_time;
    universe.regime_index   = regime_index;

    if (regime_index == 3 || regime_index == 4) {
        universe.density_field = std::move(state.field);
        if (universe.density_field.NX > 0) {
            int N = universe.density_field.NX;
            universe.velocity_x.resize(N, N, N);
            universe.velocity_y.resize(N, N, N);
            universe.velocity_z.resize(N, N, N);
        }
    }
}

// ── Tick: verificar e acionar transições automáticas ─────────────────────────────

void RegimeManager::tick(CosmicClock& clock, Universe& universe) {
    if (in_transition_) {
        // Avançar temporizador de mistura (baseado em tempo real do delta de quadro)
        // Usamos um incremento simples — chamador passa dt real via checkAndTransition
        transition_elapsed_ += 1.0f / 60.0f;  // assume 60fps; suficientemente preciso
        transition_t_ = std::clamp(transition_elapsed_ / transition_dur_, 0.0f, 1.0f);
        if (transition_t_ >= 1.0f) {
            in_transition_ = false;
            active_index_  = transition_to_;
            std::printf("[REGIME] Transition %d→%d complete.\n",
                        transition_from_, transition_to_);
        }
        return;
    }

    // Atualizar física do regime atual
    if (regimes_[active_index_]) {
// Atualização de física é chamada pelo loop principal (após clock.step),
        // não por tick(). tick() apenas trata verificações de transição.
        (void)universe;
    }

    checkAndTransition(clock, universe);

    // Sincronizar estado do universo
    universe.active_particles = static_cast<int>(
        std::count_if(universe.particles.flags.begin(),
                      universe.particles.flags.end(),
                      [](uint32_t f){ return f & PF_ACTIVE; }));
}

void RegimeManager::checkAndTransition(CosmicClock& clock, Universe& universe) {
    int new_regime = clock.getCurrentRegimeIndex();
    if (new_regime != active_index_ && !in_transition_) {
        beginTransition(active_index_, new_regime, universe, clock);
    }
}

void RegimeManager::beginTransition(int from, int to, Universe& universe,
                                     CosmicClock& clock)
{
    std::printf("[REGIME] Transitioning %d→%d at T=%.4e keV, t=%.4e s\n",
                from, to, clock.getTemperatureKeV(), clock.getCosmicTime());

    // Sair do regime atual
    if (regimes_[from]) regimes_[from]->onExit();

    // Construir estado inicial para o novo regime
    InitialState st = buildInitialState(to);
    applyInitialState(to, st, universe);

    // Entrar no novo regime
    if (regimes_[to]) regimes_[to]->onEnter(universe);

    in_transition_     = true;
    transition_from_   = from;
    transition_to_     = to;
    transition_t_      = 0.0f;
    transition_elapsed_= 0.0f;
    active_index_      = to;  // física executa no novo regime imediatamente
}

// ── Navegação ───────────────────────────────────────────────────────────────

void RegimeManager::jumpToRegime(int index, CosmicClock& clock, Universe& universe) {
    int idx = std::clamp(index, 0, 4);
    if (regimes_[active_index_]) regimes_[active_index_]->onExit();

    clock.jumpToRegime(idx);

    InitialState st = buildInitialState(idx);
    applyInitialState(idx, st, universe);
    active_index_ = idx;
    in_transition_ = false;

    if (regimes_[idx]) regimes_[idx]->onEnter(universe);

    std::printf("[REGIME] Jumped to regime %d.\n", idx);
}

// ── Renderização ───────────────────────────────────────────────────────────────────

void RegimeManager::render(Renderer& renderer, const Universe& universe) {
    if (!regimes_[active_index_]) return;

    if (in_transition_ && regimes_[transition_from_]) {
        // Durante transição, ambos os regimes renderizam (mistura tratada pelo renderizador)
        renderer.setRegimeBlend(transition_from_, transition_to_, transition_t_);
    } else {
        renderer.setRegimeBlend(active_index_, active_index_, 0.0f);
    }

    regimes_[active_index_]->render(renderer, universe);
}

IRegime* RegimeManager::getCurrentRegime() const {
    return regimes_[active_index_].get();
}
