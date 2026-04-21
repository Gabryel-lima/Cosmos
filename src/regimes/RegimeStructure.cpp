// src/regimes/RegimeStructure.cpp — Regime 4: Formação de Estruturas
#include "RegimeStructure.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/Universe.hpp"
#include "../render/Renderer.hpp"
#include "../physics/Constants.hpp"
#include "../physics/Friedmann.hpp"
#include "../physics/ParticlePool.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <numeric>
#include <random>

namespace {
float stellarGlowFromMass(double m_ratio) {
    double clamped_mass = std::clamp(m_ratio, 0.1, 40.0);
    return static_cast<float>(1.2 + std::log10(1.0 + std::pow(clamped_mass, 2.2)) * 1.8);
}
}

void RegimeStructure::onEnter(Universe& state) {
    prev_scale_factor_ = state.scale_factor;
    halos_.clear();
    last_fof_time_ = state.cosmic_time;

    // Aplica configurações padrão de N-body do perfil de qualidade
    nbody_.theta     = state.quality.barnes_hut_theta;
    nbody_.softening = 0.25f;
}

void RegimeStructure::onExit() {
    halos_.clear();
}

// ── Integrador Leapfrog (Kick-Drift-Kick) ───────────────────────────────────

void RegimeStructure::leapfrogKick(Universe& universe, double dt) {
    // v_{n+1/2} = v_n + (F_n/m) * dt/2
    ParticlePool& p = universe.particles;
    size_t n = p.x.size();

    std::vector<double> ax, ay, az;
    nbody_.computeForces(p, ax, ay, az);

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        double amag = std::sqrt(ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i]);
        if (amag > 0.15) {
            double scale = 0.15 / amag;
            ax[i] *= scale;
            ay[i] *= scale;
            az[i] *= scale;
        }
        p.vx[i] += ax[i] * dt * 0.5;
        p.vy[i] += ay[i] * dt * 0.5;
        p.vz[i] += az[i] * dt * 0.5;
    }
}

void RegimeStructure::leapfrogDrift(Universe& universe, double dt) {
    ParticlePool& p = universe.particles;
    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        p.x[i] += p.vx[i] * dt;
        p.y[i] += p.vy[i] * dt;
        p.z[i] += p.vz[i] * dt;
    }
}

void RegimeStructure::applyCosmicExpansion(Universe& universe, double a_prev, double a_new) {
    if (a_prev <= 0.0) return;
    double ratio = a_new / a_prev;
    ParticlePool& p = universe.particles;
    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        p.x[i] *= ratio; p.y[i] *= ratio; p.z[i] *= ratio;
        p.vx[i] /= ratio; p.vy[i] /= ratio; p.vz[i] /= ratio;
    }
}

// ── Formação estelar: critério de densidade local ─────────────────────────────────────────────

void RegimeStructure::checkStarFormation(Universe& universe, double /*temp_K*/) {
    ParticlePool& p = universe.particles;
    size_t n = p.x.size();

    // Raio de busca: ~2% do lado da caixa (≈1 Mpc em unidades de sim)
    constexpr double R2_THRESH  = 1.0 * 1.0;   // sim units²
    constexpr int    MIN_NEIGH  = 6;            // mínimo de vizinhos para colapso

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        if (p.type[i] != ParticleType::GAS && p.type[i] != ParticleType::DARK_MATTER) continue;
        if (p.flags[i] & PF_COLLAPSING) continue;

        int neighbors = 0;
        for (size_t j = 0; j < n; ++j) {
            if (i == j || !(p.flags[j] & PF_ACTIVE)) continue;
            double dx = p.x[j]-p.x[i], dy = p.y[j]-p.y[i], dz = p.z[j]-p.z[i];
            if (dx*dx + dy*dy + dz*dz < R2_THRESH) {
                if (++neighbors >= MIN_NEIGH) break;  // encontrou o suficiente
            }
        }
        if (neighbors < MIN_NEIGH) continue;

        // Acima do limiar de densidade: criar protoestrela
        p.flags[i] |= PF_STAR_FORMED;
        p.type[i]   = ParticleType::STAR;
        p.star_state[i] = StarState::PROTOSTAR;
        p.star_age[i]   = 0.0;
        // Massa estelar aleatória 0.1–40 M☉ (em kg, normalizada pela simulação)
        static std::mt19937 rng_sf(777);
        std::uniform_real_distribution<double> mass_dist(0.1, 40.0);
        p.mass[i] = mass_dist(rng_sf) * 1.989e30;  // kg
        // Protoestrela: amarelo-branco
        p.color_r[i] = 1.0f; p.color_g[i] = 0.9f; p.color_b[i] = 0.7f;
        p.luminosity[i] = 1.0f;
    }
}

// ── Máquina de estados do ciclo de vida estelar ────────────────────────────────────────────

void RegimeStructure::updateStellarEvolution(Universe& universe, double cosmic_dt) {
    ParticlePool& p = universe.particles;
    size_t n = p.x.size();

    // t_MS = (M/L) * t_sun, L ∝ M^3.5, portanto t_MS ∝ M^{-2.5}
    constexpr double t_sun  = 1.0e10 * phys::yr_to_s;  // tempo de vida na SM do sol
    constexpr double M_sun  = 1.989e30;                  // [kg]

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        if (p.type[i] != ParticleType::STAR) continue;

        p.star_age[i] += cosmic_dt;
        double m_ratio = p.mass[i] / M_sun;
        if (m_ratio <= 0.0) continue;
        double t_ms = t_sun * std::pow(m_ratio, -2.5);

        double age = p.star_age[i];
        StarState& state = p.star_state[i];

        switch (state) {
            case StarState::PROTOSTAR:
                if (age > t_ms * 0.001) {
                    state = StarState::MAIN_SEQUENCE;
                    // Cor codifica tipo espectral (OBAFGKM)
                    // O: azul-branco, G: amarelo, M: vermelho
                    float t_norm = static_cast<float>(std::clamp(m_ratio, 0.1, 30.0) / 30.0f);
                    p.color_r[i] = 1.0f - t_norm * 0.8f;
                    p.color_g[i] = 0.8f - t_norm * 0.3f;
                    p.color_b[i] = 0.5f + t_norm * 0.5f;
                    p.luminosity[i] = stellarGlowFromMass(m_ratio);
                }
                break;
            case StarState::MAIN_SEQUENCE:
                if (age > t_ms) {
                    state = (m_ratio > 8.0) ? StarState::BLUE_GIANT : StarState::RED_GIANT;
                    p.color_r[i] = 1.0f; p.color_g[i] = 0.3f; p.color_b[i] = 0.1f;
                    p.luminosity[i] = std::min(p.luminosity[i] * 2.5f, 8.0f);
                }
                break;
            case StarState::RED_GIANT:
            case StarState::BLUE_GIANT:
                if (age > t_ms * 1.2) {
                    if (m_ratio > 8.0) {
                        // Supernova: ejeta massa, impulsiona vizinhos
                        state = StarState::BLACK_HOLE;
                        p.type[i] = ParticleType::BLACKHOLE;
                        p.color_r[i] = 0.0f; p.color_g[i] = 0.0f; p.color_b[i] = 0.0f;
                        p.luminosity[i] = 5.0f;  // brilho de acreção
                        // Impulso de velocidade às partículas próximas
                        for (size_t j = 0; j < n; ++j) {
                            if (j == i || !(p.flags[j] & PF_ACTIVE)) continue;
                            double dx = p.x[j]-p.x[i], dy = p.y[j]-p.y[i], dz = p.z[j]-p.z[i];
                            double r = std::sqrt(dx*dx+dy*dy+dz*dz) + 0.001;
                            if (r < 0.1) {
                                double kick = 100.0 / (r + 0.01);
                                p.vx[j] += kick * dx/r;
                                p.vy[j] += kick * dy/r;
                                p.vz[j] += kick * dz/r;
                            }
                        }
                    } else {
                        state = StarState::WHITE_DWARF;
                        p.color_r[i] = 0.9f; p.color_g[i] = 0.9f; p.color_b[i] = 1.0f;
                        p.luminosity[i] = 0.01f;
                        p.mass[i] *= 0.5;
                    }
                }
                break;
            default:
                break;
        }
    }
}

// ── Identificador de galáxias Friends-of-Friends ───────────────────────────────────────

void RegimeStructure::runFriendsOfFriends(Universe& universe) {
    const ParticlePool& p = universe.particles;
    size_t n = p.x.size();
    if (n == 0) return;

    halos_.clear();
    double b = 0.2;  // fração do comprimento de ligação

    // Separação média entre partículas (unidades de sim)
    double n_bar = static_cast<double>(n);
    double link2 = std::pow(b * std::pow(n_bar, -1.0/3.0), 2.0);

    std::vector<int> group(n, -1);
    int group_id = 0;

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        if (group[i] >= 0) continue;
        group[i] = group_id;
        std::vector<size_t> stack = {i};
        while (!stack.empty()) {
            size_t cur = stack.back(); stack.pop_back();
            for (size_t j = 0; j < n; ++j) {
                if (group[j] >= 0 || !(p.flags[j] & PF_ACTIVE)) continue;
                double dx = p.x[j]-p.x[cur], dy = p.y[j]-p.y[cur], dz = p.z[j]-p.z[cur];
                double r2 = dx*dx+dy*dy+dz*dz;
                if (r2 < link2) {
                    group[j] = group_id;
                    stack.push_back(j);
                }
            }
        }
        ++group_id;
    }

    // Constrói lista de halos a partir da associação de grupos
    std::vector<Halo> new_halos(static_cast<size_t>(group_id));
    for (size_t i = 0; i < n; ++i) {
        if (group[i] < 0 || !(p.flags[i] & PF_ACTIVE)) continue;
        auto& h = new_halos[static_cast<size_t>(group[i])];
        h.cx += p.x[i] * p.mass[i];
        h.cy += p.y[i] * p.mass[i];
        h.cz += p.z[i] * p.mass[i];
        h.mass += p.mass[i];
        h.member_count++;
    }
    for (auto& h : new_halos) {
        if (h.mass > 0.0 && h.member_count >= 8) {
            h.cx /= h.mass; h.cy /= h.mass; h.cz /= h.mass;
            halos_.push_back(h);
        }
    }
}

// ── Atualização principal ─────────────────────────────────────────────────────

void RegimeStructure::update(double cosmic_dt, double scale_factor, double temp_keV,
                              Universe& universe)
{
    double a_new = scale_factor;
    double T_K   = phys::keV_to_K(temp_keV);
    constexpr double regime_duration = phys::t_today - CosmicClock::REGIME_START_TIMES[4];
    double progress_dt = (regime_duration > 0.0) ? cosmic_dt / regime_duration : 0.0;
    double visual_dt = cosmic_dt <= 0.0 ? 0.0
                                         : std::clamp(progress_dt * 48.0, 0.0005, 0.02);
    double stellar_dt = cosmic_dt <= 0.0 ? 0.0
                                          : progress_dt * (5.0e8 * phys::yr_to_s);

    // Integração Leapfrog KDK
    leapfrogKick(universe, visual_dt);
    leapfrogDrift(universe, visual_dt);

    std::vector<double> ax, ay, az;
    nbody_.computeForces(universe.particles, ax, ay, az);
    // Segundo kick
    ParticlePool& p = universe.particles;
    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        double amag = std::sqrt(ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i]);
        if (amag > 0.15) {
            double scale = 0.15 / amag;
            ax[i] *= scale;
            ay[i] *= scale;
            az[i] *= scale;
        }
        p.vx[i] += ax[i] * visual_dt * 0.5;
        p.vy[i] += ay[i] * visual_dt * 0.5;
        p.vz[i] += az[i] * visual_dt * 0.5;
    }

    applyCosmicExpansion(universe, prev_scale_factor_, a_new);
    prev_scale_factor_ = a_new;

    // Formação estelar a cada 60 frames (O(N²) por isso limitado)
    if (++star_check_frame_ % 20 == 0) {
        checkStarFormation(universe, T_K);
    }
    updateStellarEvolution(universe, stellar_dt);

    // Executa FoF a cada 1e14 segundos (tempo cósmico)
    if (universe.cosmic_time - last_fof_time_ > 1e14) {
        runFriendsOfFriends(universe);
        last_fof_time_ = universe.cosmic_time;
    }

    universe.scale_factor    = scale_factor;
    universe.temperature_keV = temp_keV;
}

void RegimeStructure::render(Renderer& renderer, const Universe& universe) {
    renderer.renderParticles(universe);
    // Converte Halo interno para HaloInfo para o renderizador
    std::vector<HaloInfo> halo_info;
    halo_info.reserve(halos_.size());
    for (const auto& h : halos_)
        halo_info.push_back({h.cx, h.cy, h.cz, h.mass, h.member_count});
    renderer.renderGalaxyHalos(halo_info.data(), static_cast<int>(halo_info.size()));
}
