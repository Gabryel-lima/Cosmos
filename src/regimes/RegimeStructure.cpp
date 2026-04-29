// src/regimes/RegimeStructure.cpp — Regime 4: Formação de Estruturas
#include "RegimeStructure.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/SimulationRandom.hpp"
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

#include <glm/vec3.hpp>

namespace {
float stellarGlowFromMass(double m_ratio) {
    double clamped_mass = std::clamp(m_ratio, 0.1, 40.0);
    return static_cast<float>(1.2 + std::log10(1.0 + std::pow(clamped_mass, 2.2)) * 1.8);
}

double phaseDurationSeconds(StructurePhase phase) {
    switch (phase) {
        case StructurePhase::DARK_AGES:
            return CosmicClock::REGIME_START_TIMES[7] - CosmicClock::REGIME_START_TIMES[6];
        case StructurePhase::REIONIZATION:
            return CosmicClock::REGIME_START_TIMES[8] - CosmicClock::REGIME_START_TIMES[7];
        case StructurePhase::MATURE:
        default:
            return phys::t_today - CosmicClock::REGIME_START_TIMES[8];
    }
}

std::mt19937& starFormationRng() {
    static std::mt19937 rng = simrng::makeStream("structure-stars");
    return rng;
}

int computeSubsteps(double total_visual_dt, double target_visual_dt, int max_substeps) {
    if (total_visual_dt <= 0.0) return 1;
    double safe_target = std::max(target_visual_dt, 1e-6);
    int substeps = static_cast<int>(std::ceil(total_visual_dt / safe_target));
    return std::clamp(substeps, 1, max_substeps);
}

double interpolatePositive(double start, double end, double alpha) {
    double a0 = std::max(start, 1e-60);
    double a1 = std::max(end, 1e-60);
    double t = std::clamp(alpha, 0.0, 1.0);
    return a0 * std::pow(a1 / a0, t);
}

double wrapPosition(double value, double box_size) {
    if (box_size <= 0.0) return value;
    double wrapped = std::fmod(value + box_size * 0.5, box_size);
    if (wrapped < 0.0) wrapped += box_size;
    return wrapped - box_size * 0.5;
}

float phaseWave(double cosmic_time, double seed, double frequency) {
    return 0.5f + 0.5f * static_cast<float>(std::sin(cosmic_time * frequency + seed));
}
}

std::string RegimeStructure::getName() const {
    switch (phase_) {
        case StructurePhase::DARK_AGES:   return "Dark Ages";
        case StructurePhase::REIONIZATION:return "Reionization";
        case StructurePhase::MATURE:      return "Structure Formation";
        default:                          return "Structure Formation";
    }
}

int RegimeStructure::regimeIndex() const {
    switch (phase_) {
        case StructurePhase::DARK_AGES: return 6;
        case StructurePhase::REIONIZATION: return 7;
        case StructurePhase::MATURE: return 8;
        default: return 8;
    }
}

void RegimeStructure::applyPhasePalette(Universe& state) const {
    ParticlePool& p = state.particles;
    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        switch (phase_) {
            case StructurePhase::DARK_AGES:
                if (p.type[i] == ParticleType::GAS) {
                    p.color_r[i] = 0.24f; p.color_g[i] = 0.55f; p.color_b[i] = 0.95f;
                    p.luminosity[i] = std::max(0.18f, p.luminosity[i] * 0.35f);
                } else if (p.type[i] == ParticleType::DARK_MATTER) {
                    p.color_r[i] = 0.45f; p.color_g[i] = 0.18f; p.color_b[i] = 0.78f;
                    p.luminosity[i] = 0.05f;
                }
                break;
            case StructurePhase::REIONIZATION:
                if (p.type[i] == ParticleType::GAS) {
                    p.color_r[i] = 0.34f; p.color_g[i] = 0.78f; p.color_b[i] = 1.0f;
                    p.luminosity[i] = std::max(0.35f, p.luminosity[i] * 0.65f);
                } else if (p.type[i] == ParticleType::STAR) {
                    p.color_r[i] = 0.82f; p.color_g[i] = 0.9f; p.color_b[i] = 1.0f;
                    p.luminosity[i] = std::max(p.luminosity[i], 4.6f);
                }
                break;
            case StructurePhase::MATURE:
                if (p.type[i] == ParticleType::STAR) {
                    p.luminosity[i] = std::max(p.luminosity[i], 3.0f);
                }
                break;
        }
    }
}

void RegimeStructure::onEnter(Universe& state) {
    prev_scale_factor_ = state.scale_factor;
    halos_.clear();
    last_fof_time_ = state.cosmic_time;
    star_check_frame_ = 0;

    // Aplica configurações padrão de N-body do perfil de qualidade
    nbody_.theta     = state.quality.barnes_hut_theta;
    nbody_.softening = 0.25f;
    applyPhasePalette(state);
}

void RegimeStructure::onExit() {
    halos_.clear();
}

void RegimeStructure::animatePhaseEmission(Universe& universe, double cosmic_time) const {
    ParticlePool& p = universe.particles;
    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;

        const float pulse = phaseWave(cosmic_time, static_cast<double>(i) * 0.073, 1.0e-15);
        switch (phase_) {
            case StructurePhase::DARK_AGES:
                if (p.type[i] == ParticleType::GAS) {
                    p.luminosity[i] = std::max(0.10f, 0.16f + pulse * 0.10f);
                    p.color_r[i] = 0.10f + 0.06f * pulse;
                    p.color_g[i] = 0.18f + 0.14f * pulse;
                    p.color_b[i] = 0.32f + 0.28f * pulse;
                } else if (p.type[i] == ParticleType::DARK_MATTER) {
                    p.luminosity[i] = 0.03f + pulse * 0.02f;
                }
                break;
            case StructurePhase::REIONIZATION:
                if (p.type[i] == ParticleType::GAS) {
                    p.luminosity[i] = std::max(0.24f, p.luminosity[i] * 0.985f + pulse * 0.18f);
                    p.color_r[i] = 0.24f + 0.12f * pulse;
                    p.color_g[i] = 0.58f + 0.18f * pulse;
                    p.color_b[i] = 0.92f + 0.08f * pulse;
                } else if (p.type[i] == ParticleType::STAR) {
                    p.luminosity[i] = std::max(p.luminosity[i], 4.4f + pulse * 1.4f);
                } else if (p.type[i] == ParticleType::BLACKHOLE) {
                    p.luminosity[i] = std::max(p.luminosity[i], 4.8f + pulse * 1.6f);
                }
                break;
            case StructurePhase::MATURE:
                if (p.type[i] == ParticleType::GAS) {
                    p.luminosity[i] = std::max(0.18f, p.luminosity[i] * 0.992f + pulse * 0.08f);
                } else if (p.type[i] == ParticleType::STAR) {
                    p.luminosity[i] = std::max(2.2f, p.luminosity[i] * 0.996f + pulse * 0.26f);
                } else if (p.type[i] == ParticleType::BLACKHOLE) {
                    p.luminosity[i] = std::max(5.2f, p.luminosity[i] * 0.997f + pulse * 0.42f);
                }
                break;
        }
    }
}

void RegimeStructure::rebuildDensityField(Universe& universe, double cosmic_time) {
    GridData& field = universe.density_field;
    GridData& vx = universe.velocity_x;
    GridData& vy = universe.velocity_y;
    GridData& vz = universe.velocity_z;
    const int N = RegimeConfig::STRUCT_GRID_SIZE;
    if (field.NX != N) {
        field.resize(N, N, N);
        vx.resize(N, N, N);
        vy.resize(N, N, N);
        vz.resize(N, N, N);
    }

    std::fill(field.data.begin(), field.data.end(), 0.0f);
    std::fill(vx.data.begin(), vx.data.end(), 0.0f);
    std::fill(vy.data.begin(), vy.data.end(), 0.0f);
    std::fill(vz.data.begin(), vz.data.end(), 0.0f);
    std::vector<float> weight(field.data.size(), 0.0f);

    const double box_size = RegimeConfig::STRUCT_BOX_SIZE_MPC;
    const double inv_box = 1.0 / box_size;
    auto depositKernel = [&](const glm::dvec3& pos, const glm::dvec3& vel,
                             float density_weight, int radius, float falloff,
                             float ionization) {
        double px = (wrapPosition(pos.x, box_size) * inv_box + 0.5) * static_cast<double>(N);
        double py = (wrapPosition(pos.y, box_size) * inv_box + 0.5) * static_cast<double>(N);
        double pz = (wrapPosition(pos.z, box_size) * inv_box + 0.5) * static_cast<double>(N);
        int cx = static_cast<int>(std::floor(px));
        int cy = static_cast<int>(std::floor(py));
        int cz = static_cast<int>(std::floor(pz));
        for (int dz = -radius; dz <= radius; ++dz)
        for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            int ix = (cx + dx + N) % N;
            int iy = (cy + dy + N) % N;
            int iz = (cz + dz + N) % N;
            float dist2 = static_cast<float>(dx * dx + dy * dy + dz * dz);
            float kernel = std::exp(-dist2 * falloff);
            float heated = kernel * density_weight * (1.0f + ionization);
            size_t idx = static_cast<size_t>(ix + N * (iy + N * iz));
            field.data[idx] += heated;
            vx.data[idx] += static_cast<float>(vel.x) * kernel;
            vy.data[idx] += static_cast<float>(vel.y) * kernel;
            vz.data[idx] += static_cast<float>(vel.z) * kernel;
            weight[idx] += kernel;
        }
    };

    ParticlePool& p = universe.particles;
    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;

        float density_weight = 0.0f;
        int radius = 1;
        float falloff = 0.75f;
        float ionization = 0.0f;

        switch (p.type[i]) {
            case ParticleType::GAS:
                density_weight = (phase_ == StructurePhase::DARK_AGES) ? 1.15f : 0.95f;
                radius = (phase_ == StructurePhase::MATURE) ? 2 : 1;
                falloff = (phase_ == StructurePhase::DARK_AGES) ? 0.95f : 0.72f;
                break;
            case ParticleType::DARK_MATTER:
                density_weight = 0.55f;
                radius = 1;
                falloff = 1.10f;
                break;
            case ParticleType::STAR:
                density_weight = 1.4f;
                radius = (phase_ == StructurePhase::REIONIZATION) ? 3 : 2;
                falloff = 0.58f;
                ionization = (phase_ == StructurePhase::REIONIZATION) ? (0.4f + 0.5f * phaseWave(cosmic_time, static_cast<double>(i), 2.4e-15)) : 0.12f;
                break;
            case ParticleType::BLACKHOLE:
                density_weight = 2.2f;
                radius = (phase_ == StructurePhase::MATURE) ? 3 : 2;
                falloff = 0.44f;
                ionization = 0.35f + 0.35f * phaseWave(cosmic_time, static_cast<double>(i) * 0.37, 3.2e-15);
                break;
            default:
                continue;
        }

        depositKernel({p.x[i], p.y[i], p.z[i]}, {p.vx[i], p.vy[i], p.vz[i]},
                      density_weight, radius, falloff, ionization);
    }

    for (const Halo& halo : halos_) {
        float halo_strength = std::clamp(static_cast<float>(halo.member_count) / 24.0f, 0.2f, 2.8f);
        float ionization = (phase_ == StructurePhase::REIONIZATION) ? halo_strength * 0.45f : halo_strength * 0.10f;
        depositKernel({halo.cx, halo.cy, halo.cz}, {0.0, 0.0, 0.0},
                      halo_strength, (phase_ == StructurePhase::MATURE) ? 4 : 3, 0.26f, ionization);
    }

    const float base_floor = (phase_ == StructurePhase::DARK_AGES) ? 0.008f : (phase_ == StructurePhase::REIONIZATION ? 0.015f : 0.010f);
    for (size_t idx = 0; idx < field.data.size(); ++idx) {
        float w = std::max(weight[idx], 1e-4f);
        vx.data[idx] /= w;
        vy.data[idx] /= w;
        vz.data[idx] /= w;
        float micro = phaseWave(cosmic_time, static_cast<double>(idx) * 0.013, 4.5e-16) - 0.5f;
        field.data[idx] = std::max(base_floor, field.data[idx] * 0.08f + micro * base_floor * 0.4f + base_floor);
    }
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
        // Removido: p.x[i] *= ratio; geométricamente isso causa fuga da câmera.
        // Simulamos e renderizamos en coordenadas comóveis = tamanho da caixa visível fixo.
        p.vx[i] /= ratio; p.vy[i] /= ratio; p.vz[i] /= ratio;
    }
}

// ── Formação estelar: critério de densidade local ─────────────────────────────────────────────

void RegimeStructure::checkStarFormation(Universe& universe, double /*temp_K*/) {
    if (phase_ == StructurePhase::DARK_AGES) return;

    ParticlePool& p = universe.particles;
    size_t n = p.x.size();

    // Raio de busca: ~2% do lado da caixa (≈1 Mpc em unidades de sim)
    const double search_radius = (phase_ == StructurePhase::REIONIZATION) ? 0.8 : 1.0;
    const double r2_thresh  = search_radius * search_radius;
    const int min_neigh = (phase_ == StructurePhase::REIONIZATION) ? 8 : 6;

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        if (p.type[i] != ParticleType::GAS) continue;
        if (p.flags[i] & PF_COLLAPSING) continue;

        int neighbors = 0;
        for (size_t j = 0; j < n; ++j) {
            if (i == j || !(p.flags[j] & PF_ACTIVE)) continue;
            double dx = p.x[j]-p.x[i], dy = p.y[j]-p.y[i], dz = p.z[j]-p.z[i];
            if (dx*dx + dy*dy + dz*dz < r2_thresh) {
                if (++neighbors >= min_neigh) break;  // encontrou o suficiente
            }
        }
        if (neighbors < min_neigh) continue;

        // Acima do limiar de densidade: criar protoestrela
        p.flags[i] |= PF_STAR_FORMED | PF_COLLAPSING;
        p.type[i]   = ParticleType::STAR;
        p.star_state[i] = StarState::PROTOSTAR;
        p.star_age[i]   = 0.0;
        // Massa estelar aleatória 0.1–40 M☉ (em kg, normalizada pela simulação)
        std::uniform_real_distribution<double> mass_dist(
            (phase_ == StructurePhase::REIONIZATION) ? 8.0 : 0.1,
            (phase_ == StructurePhase::REIONIZATION) ? 60.0 : 40.0);
        p.mass[i] = mass_dist(starFormationRng()) * 1.989e30;  // kg
        if (phase_ == StructurePhase::REIONIZATION) {
            p.color_r[i] = 0.82f; p.color_g[i] = 0.9f; p.color_b[i] = 1.0f;
            p.luminosity[i] = 4.8f;
        } else {
            p.color_r[i] = 1.0f; p.color_g[i] = 0.9f; p.color_b[i] = 0.7f;
            p.luminosity[i] = 1.0f;
        }
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
                        ParticlePool::defaultColor(ParticleType::BLACKHOLE,
                                                   p.color_r[i], p.color_g[i], p.color_b[i]);
                        p.luminosity[i] = 5.0f;  // brilho de acreção
                        // Impulso de velocidade às partículas próximas
                        for (size_t j = 0; j < n; ++j) {
                            if (j == i || !(p.flags[j] & PF_ACTIVE)) continue;
                            double dx = p.x[j]-p.x[i], dy = p.y[j]-p.y[i], dz = p.z[j]-p.z[i];
                            double r = std::sqrt(dx*dx+dy*dy+dz*dz) + 0.001;
                            if (r < 0.25) { // Ampliado levemente o raio
                                // Reduzido drasticamente o kick para evitar explosão (fuga da câmera)
                                double kick = 0.2 / (r + 0.05); 
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
    double a_prev_frame = std::max(prev_scale_factor_, 1e-60);
    double a_new = std::max(scale_factor, 1e-60);
    double T_K   = phys::keV_to_K(temp_keV);
    const double regime_duration = phaseDurationSeconds(phase_);
    double progress_dt = (regime_duration > 0.0) ? cosmic_dt / regime_duration : 0.0;
    double visual_multiplier = (phase_ == StructurePhase::DARK_AGES) ? 24.0 : (phase_ == StructurePhase::REIONIZATION ? 36.0 : 48.0);
    double total_visual_dt = cosmic_dt <= 0.0 ? 0.0
                                               : std::clamp(progress_dt * visual_multiplier, 0.0005, 0.02);
    double stellar_dt_scale = (phase_ == StructurePhase::REIONIZATION) ? 2.0e8 : 5.0e8;
    double total_stellar_dt = cosmic_dt <= 0.0 ? 0.0
                                                : progress_dt * (stellar_dt_scale * phys::yr_to_s);
    int substeps = computeSubsteps(total_visual_dt, 0.005, 6);
    double sub_visual_dt = total_visual_dt / static_cast<double>(substeps);
    double sub_stellar_dt = total_stellar_dt / static_cast<double>(substeps);

    for (int step = 0; step < substeps; ++step) {
        double alpha0 = static_cast<double>(step) / static_cast<double>(substeps);
        double alpha1 = static_cast<double>(step + 1) / static_cast<double>(substeps);
        double a_step_prev = interpolatePositive(a_prev_frame, a_new, alpha0);
        double a_step_new = interpolatePositive(a_prev_frame, a_new, alpha1);

        // Integração Leapfrog KDK
        leapfrogKick(universe, sub_visual_dt);
        leapfrogDrift(universe, sub_visual_dt);

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
            p.vx[i] += ax[i] * sub_visual_dt * 0.5;
            p.vy[i] += ay[i] * sub_visual_dt * 0.5;
            p.vz[i] += az[i] * sub_visual_dt * 0.5;
        }

        applyCosmicExpansion(universe, a_step_prev, a_step_new);

        if (phase_ != StructurePhase::DARK_AGES) {
            updateStellarEvolution(universe, sub_stellar_dt);
        }
    }

    animatePhaseEmission(universe, universe.cosmic_time);

    prev_scale_factor_ = a_new;

    // Formação estelar a cada 60 frames (O(N²) por isso limitado)
    int star_check_interval = (phase_ == StructurePhase::REIONIZATION) ? 32 : 20;
    if (phase_ != StructurePhase::DARK_AGES && ++star_check_frame_ % star_check_interval == 0) {
        checkStarFormation(universe, T_K);
    }

    // Executa FoF a cada 1e14 segundos (tempo cósmico)
    if (universe.cosmic_time - last_fof_time_ > 1e14) {
        runFriendsOfFriends(universe);
        last_fof_time_ = universe.cosmic_time;
    }

    rebuildDensityField(universe, universe.cosmic_time);

    universe.scale_factor    = scale_factor;
    universe.temperature_keV = temp_keV;
}

void RegimeStructure::render(Renderer& renderer, const Universe& universe) {
    renderer.renderVolumeField(universe); // Mostrar a teia/poeira cósmica de gás em fundo
    renderer.renderParticles(universe);
    // Converte Halo interno para HaloInfo para o renderizador
    std::vector<HaloInfo> halo_info;
    halo_info.reserve(halos_.size());
    for (const auto& h : halos_)
        halo_info.push_back({h.cx, h.cy, h.cz, h.mass, h.member_count});
    renderer.renderGalaxyHalos(halo_info.data(), static_cast<int>(halo_info.size()));
}
