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

glm::dvec3 safeNormalize(const glm::dvec3& value, const glm::dvec3& fallback) {
    double len2 = glm::dot(value, value);
    if (len2 <= 1e-12 || !std::isfinite(len2)) return fallback;
    return value / std::sqrt(len2);
}

glm::dvec3 preferredFrontDirection(const ParticlePool& particles, size_t index, StructurePhase phase) {
    const glm::dvec3 velocity = {particles.vx[index], particles.vy[index], particles.vz[index]};
    const double seed = static_cast<double>(index) * 0.131;
    const glm::dvec3 seeded = safeNormalize(
        glm::dvec3(std::sin(seed), std::cos(seed * 1.7), std::sin(seed * 0.7 + 1.2)),
        glm::dvec3(0.0, 0.0, 1.0));
    if (glm::dot(velocity, velocity) > 1e-8) {
        return safeNormalize(velocity, seeded);
    }

    const glm::dvec3 radial = {particles.x[index], particles.y[index], particles.z[index]};
    if (phase == StructurePhase::REIONIZATION) {
        return safeNormalize(radial + seeded * 0.35, seeded);
    }
    return safeNormalize(seeded, glm::dvec3(0.0, 0.0, 1.0));
}

float sampleGridWrapped(const GridData& field, double x, double y, double z, double box_size) {
    if (field.NX <= 0 || field.data.empty() || box_size <= 0.0) return 0.0f;
    const double inv_box = 1.0 / box_size;
    double fx = (wrapPosition(x, box_size) * inv_box + 0.5) * static_cast<double>(field.NX);
    double fy = (wrapPosition(y, box_size) * inv_box + 0.5) * static_cast<double>(field.NY);
    double fz = (wrapPosition(z, box_size) * inv_box + 0.5) * static_cast<double>(field.NZ);
    int ix = (static_cast<int>(std::floor(fx)) % field.NX + field.NX) % field.NX;
    int iy = (static_cast<int>(std::floor(fy)) % field.NY + field.NY) % field.NY;
    int iz = (static_cast<int>(std::floor(fz)) % field.NZ + field.NZ) % field.NZ;
    return field.at(ix, iy, iz);
}

void diffusePeriodicField(GridData& field, int passes, float center_weight, float neighbor_weight) {
    if (field.NX <= 0 || field.data.empty() || passes <= 0) return;
    std::vector<float> scratch(field.data.size(), 0.0f);
    const int nx = field.NX;
    const int ny = field.NY;
    const int nz = field.NZ;

    for (int pass = 0; pass < passes; ++pass) {
        std::fill(scratch.begin(), scratch.end(), 0.0f);
        for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x) {
            float accum = field.at(x, y, z) * center_weight;
            float weight = center_weight;
            for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                const int ix = (x + dx + nx) % nx;
                const int iy = (y + dy + ny) % ny;
                const int iz = (z + dz + nz) % nz;
                accum += field.at(ix, iy, iz) * neighbor_weight;
                weight += neighbor_weight;
            }
            scratch[static_cast<size_t>(x + nx * (y + ny * z))] = accum / std::max(weight, 1e-6f);
        }
        field.data.swap(scratch);
    }
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

void RegimeStructure::applyRadiativeFeedback(Universe& universe, double dt) {
    if (phase_ == StructurePhase::DARK_AGES || dt <= 0.0) return;

    ParticlePool& p = universe.particles;
    const double force = std::clamp(static_cast<double>(universe.visual.reionization_ionization_force), 0.15, 3.5);
    const double anisotropy = std::clamp(static_cast<double>(universe.visual.reionization_front_anisotropy), 0.0, 2.5);
    const double base_radius = (phase_ == StructurePhase::REIONIZATION) ? 1.6 : 1.1;
    size_t processed_sources = 0;

    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        if (p.type[i] != ParticleType::STAR && p.type[i] != ParticleType::BLACKHOLE) continue;
        if (phase_ == StructurePhase::MATURE && p.type[i] == ParticleType::STAR && (i % 3) != 0) continue;
        if (++processed_sources > 96) break;

        const double source_weight = (p.type[i] == ParticleType::BLACKHOLE) ? 1.55 : 1.0;
        const double radius = base_radius * source_weight * force;
        const double radius2 = radius * radius;
        const glm::dvec3 front_dir = preferredFrontDirection(p, i, phase_);

        for (size_t j = 0; j < p.x.size(); ++j) {
            if (i == j || !(p.flags[j] & PF_ACTIVE) || p.type[j] != ParticleType::GAS) continue;

            glm::dvec3 delta = {p.x[j] - p.x[i], p.y[j] - p.y[i], p.z[j] - p.z[i]};
            const double dist2 = glm::dot(delta, delta);
            if (dist2 > radius2 || dist2 <= 1e-10) continue;

            const double dist = std::sqrt(dist2);
            const glm::dvec3 radial = safeNormalize(delta, front_dir);
            const double forward = std::max(0.0, glm::dot(radial, front_dir));
            const float local_ionization = sampleGridWrapped(universe.ionization_field,
                                                             p.x[j], p.y[j], p.z[j],
                                                             RegimeConfig::STRUCT_BOX_SIZE_MPC);
            const float local_emissivity = sampleGridWrapped(universe.emissivity_field,
                                                             p.x[j], p.y[j], p.z[j],
                                                             RegimeConfig::STRUCT_BOX_SIZE_MPC);
            const double directional_boost = 1.0 + anisotropy * forward * forward;
            const double ionization_coupling = 0.70 + static_cast<double>(local_ionization) * 0.95
                                             + static_cast<double>(local_emissivity) * 0.45;
            const double kick = dt * force * source_weight * directional_boost * ionization_coupling / (dist + 0.25)
                              * ((phase_ == StructurePhase::REIONIZATION) ? 0.16 : 0.09);

            p.vx[j] += (front_dir.x * 0.72 + radial.x * 0.38) * kick;
            p.vy[j] += (front_dir.y * 0.72 + radial.y * 0.38) * kick;
            p.vz[j] += (front_dir.z * 0.72 + radial.z * 0.38) * kick;
            p.luminosity[j] = std::max(p.luminosity[j], static_cast<float>((phase_ == StructurePhase::REIONIZATION ? 0.28 : 0.20) + kick * 1.8));

            if (phase_ == StructurePhase::REIONIZATION) {
                p.color_r[j] = std::clamp(p.color_r[j] * 0.78f + 0.16f * static_cast<float>(forward) + 0.14f * local_emissivity, 0.0f, 1.0f);
                p.color_g[j] = std::clamp(p.color_g[j] * 0.74f + 0.12f * static_cast<float>(forward) + 0.14f * local_ionization, 0.0f, 1.0f);
                p.color_b[j] = std::clamp(p.color_b[j] * 0.64f + 0.18f * static_cast<float>(forward) + 0.16f * local_ionization, 0.0f, 1.0f);
            }
        }
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
                    p.color_r[i] = 0.36f; p.color_g[i] = 0.23f; p.color_b[i] = 0.11f;
                    p.luminosity[i] = std::clamp(p.luminosity[i] * 0.35f + 0.03f, 0.03f, 0.14f);
                } else if (p.type[i] == ParticleType::DARK_MATTER) {
                    p.color_r[i] = 0.18f; p.color_g[i] = 0.14f; p.color_b[i] = 0.18f;
                    p.luminosity[i] = 0.03f;
                }
                break;
            case StructurePhase::REIONIZATION:
                if (p.type[i] == ParticleType::GAS) {
                    p.color_r[i] = 0.44f; p.color_g[i] = 0.28f; p.color_b[i] = 0.15f;
                    p.luminosity[i] = std::clamp(p.luminosity[i] * 0.42f + 0.05f, 0.05f, 0.20f);
                } else if (p.type[i] == ParticleType::STAR) {
                    p.color_r[i] = 1.0f; p.color_g[i] = 0.90f; p.color_b[i] = 0.78f;
                    p.luminosity[i] = std::max(p.luminosity[i], 3.0f);
                }
                break;
            case StructurePhase::MATURE:
                if (p.type[i] == ParticleType::GAS) {
                    p.color_r[i] = 0.50f; p.color_g[i] = 0.32f; p.color_b[i] = 0.18f;
                    p.luminosity[i] = std::clamp(p.luminosity[i] * 0.48f + 0.08f, 0.08f, 0.26f);
                } else if (p.type[i] == ParticleType::DARK_MATTER) {
                    p.color_r[i] = 0.20f; p.color_g[i] = 0.16f; p.color_b[i] = 0.20f;
                    p.luminosity[i] = 0.04f;
                }
                if (p.type[i] == ParticleType::STAR) {
                    p.color_r[i] = 1.0f; p.color_g[i] = 0.94f; p.color_b[i] = 0.82f;
                    p.luminosity[i] = std::max(p.luminosity[i], 3.4f);
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
                    p.luminosity[i] = std::max(0.26f, 0.36f + pulse * 0.16f);
                    p.color_r[i] = 0.15f + 0.10f * pulse;
                    p.color_g[i] = 0.28f + 0.18f * pulse;
                    p.color_b[i] = 0.46f + 0.32f * pulse;
                } else if (p.type[i] == ParticleType::DARK_MATTER) {
                    p.luminosity[i] = 0.06f + pulse * 0.03f;
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
    GridData& ionization = universe.ionization_field;
    GridData& emissivity = universe.emissivity_field;
    GridData& vx = universe.velocity_x;
    GridData& vy = universe.velocity_y;
    GridData& vz = universe.velocity_z;
    const int N = RegimeConfig::STRUCT_GRID_SIZE;
    auto ensureGrid = [N](GridData& grid) {
        if (grid.NX != N) grid.resize(N, N, N);
    };
    ensureGrid(field);
    ensureGrid(ionization);
    ensureGrid(emissivity);
    ensureGrid(vx);
    ensureGrid(vy);
    ensureGrid(vz);

    std::fill(field.data.begin(), field.data.end(), 0.0f);
    std::fill(ionization.data.begin(), ionization.data.end(), 0.0f);
    std::fill(emissivity.data.begin(), emissivity.data.end(), 0.0f);
    std::fill(vx.data.begin(), vx.data.end(), 0.0f);
    std::fill(vy.data.begin(), vy.data.end(), 0.0f);
    std::fill(vz.data.begin(), vz.data.end(), 0.0f);
    std::vector<float> weight(field.data.size(), 0.0f);
    std::vector<float> ionization_weight(field.data.size(), 0.0f);
    std::vector<float> emissivity_weight(field.data.size(), 0.0f);
    const float ionization_force = std::clamp(universe.visual.reionization_ionization_force, 0.15f, 3.5f);
    const float front_anisotropy = std::clamp(universe.visual.reionization_front_anisotropy, 0.0f, 2.5f);

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

    auto depositScalarKernel = [&](GridData& target, std::vector<float>& target_weight,
                                   const glm::dvec3& pos, float strength, int radius, float falloff) {
        if (strength <= 0.0f) return;
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
            size_t idx = static_cast<size_t>(ix + N * (iy + N * iz));
            target.data[idx] += kernel * strength;
            target_weight[idx] += kernel;
        }
    };

    auto depositBubbleKernel = [&](GridData& target, std::vector<float>& target_weight,
                                   const glm::dvec3& pos, float radius_cells,
                                   float strength, float shell_boost) {
        if (radius_cells <= 0.0f || strength <= 0.0f) return;
        double px = (wrapPosition(pos.x, box_size) * inv_box + 0.5) * static_cast<double>(N);
        double py = (wrapPosition(pos.y, box_size) * inv_box + 0.5) * static_cast<double>(N);
        double pz = (wrapPosition(pos.z, box_size) * inv_box + 0.5) * static_cast<double>(N);
        int cx = static_cast<int>(std::floor(px));
        int cy = static_cast<int>(std::floor(py));
        int cz = static_cast<int>(std::floor(pz));
        int reach = std::max(1, static_cast<int>(std::ceil(radius_cells)));
        for (int dz = -reach; dz <= reach; ++dz)
        for (int dy = -reach; dy <= reach; ++dy)
        for (int dx = -reach; dx <= reach; ++dx) {
            const float radial = std::sqrt(static_cast<float>(dx * dx + dy * dy + dz * dz)) / radius_cells;
            if (radial > 1.35f) continue;
            const float shell = std::exp(-22.0f * (radial - 0.82f) * (radial - 0.82f));
            const float bubble = std::exp(-4.6f * radial * radial);
            const float kernel = bubble * strength + shell * shell_boost;
            int ix = (cx + dx + N) % N;
            int iy = (cy + dy + N) % N;
            int iz = (cz + dz + N) % N;
            size_t idx = static_cast<size_t>(ix + N * (iy + N * iz));
            target.data[idx] += kernel;
            target_weight[idx] += bubble + shell * 0.35f;
        }
    };

    auto depositFrontKernel = [&](const glm::dvec3& pos, const glm::dvec3& dir,
                                  float strength, int reach, float length_bias, float width_falloff) {
        double px = (wrapPosition(pos.x, box_size) * inv_box + 0.5) * static_cast<double>(N);
        double py = (wrapPosition(pos.y, box_size) * inv_box + 0.5) * static_cast<double>(N);
        double pz = (wrapPosition(pos.z, box_size) * inv_box + 0.5) * static_cast<double>(N);
        int cx = static_cast<int>(std::floor(px));
        int cy = static_cast<int>(std::floor(py));
        int cz = static_cast<int>(std::floor(pz));
        glm::dvec3 ndir = safeNormalize(dir, glm::dvec3(0.0, 0.0, 1.0));

        for (int dz = -reach; dz <= reach; ++dz)
        for (int dy = -reach; dy <= reach; ++dy)
        for (int dx = -reach; dx <= reach; ++dx) {
            glm::dvec3 cell_offset(dx, dy, dz);
            double longitudinal = glm::dot(cell_offset, ndir);
            if (longitudinal < -1.0) continue;
            double perp2 = std::max(0.0, glm::dot(cell_offset, cell_offset) - longitudinal * longitudinal);
            float forward = std::clamp(static_cast<float>((longitudinal + 1.0) / (static_cast<double>(reach) + 1.0)), 0.0f, 1.0f);
            float cone = std::exp(-static_cast<float>(perp2) * width_falloff)
                       * std::exp(-static_cast<float>((longitudinal - static_cast<double>(reach) * length_bias)
                                                     * (longitudinal - static_cast<double>(reach) * length_bias)) * 0.14f);
            int ix = (cx + dx + N) % N;
            int iy = (cy + dy + N) % N;
            int iz = (cz + dz + N) % N;
            size_t idx = static_cast<size_t>(ix + N * (iy + N * iz));
            float contribution = strength * cone * (0.20f + 0.80f * forward);
            field.data[idx] += contribution;
            weight[idx] += cone * 0.35f;
            ionization.data[idx] += contribution * (0.55f + 0.45f * strength);
            ionization_weight[idx] += cone * 0.45f;
            emissivity.data[idx] += contribution * 0.18f;
            emissivity_weight[idx] += cone * 0.16f;
        }
    };

    ParticlePool& p = universe.particles;
    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;

        float density_weight = 0.0f;
        int radius = 1;
        float falloff = 0.75f;
        float ionization_boost = 0.0f;

        switch (p.type[i]) {
            case ParticleType::GAS:
                density_weight = (phase_ == StructurePhase::DARK_AGES) ? 1.55f : 0.95f;
                radius = (phase_ == StructurePhase::MATURE) ? 2 : 1;
                falloff = (phase_ == StructurePhase::DARK_AGES) ? 0.82f : 0.72f;
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
                ionization_boost = (phase_ == StructurePhase::REIONIZATION)
                    ? (0.4f + 0.5f * phaseWave(cosmic_time, static_cast<double>(i), 2.4e-15)) * ionization_force
                    : 0.12f * ionization_force;
                break;
            case ParticleType::BLACKHOLE:
                density_weight = 2.2f;
                radius = (phase_ == StructurePhase::MATURE) ? 3 : 2;
                falloff = 0.44f;
                ionization_boost = (0.35f + 0.35f * phaseWave(cosmic_time, static_cast<double>(i) * 0.37, 3.2e-15)) * ionization_force;
                break;
            default:
                continue;
        }

        depositKernel({p.x[i], p.y[i], p.z[i]}, {p.vx[i], p.vy[i], p.vz[i]},
                      density_weight, radius, falloff, ionization_boost);

        if (p.type[i] == ParticleType::STAR || p.type[i] == ParticleType::BLACKHOLE) {
            const float source_strength = (p.type[i] == ParticleType::BLACKHOLE ? 0.58f : 0.34f)
                                        * std::max(0.35f, p.luminosity[i])
                                        * ((phase_ == StructurePhase::REIONIZATION) ? 1.0f : 0.62f);
            depositScalarKernel(emissivity, emissivity_weight,
                                {p.x[i], p.y[i], p.z[i]},
                                source_strength,
                                (phase_ == StructurePhase::REIONIZATION) ? 3 : 2,
                                (phase_ == StructurePhase::REIONIZATION) ? 0.28f : 0.36f);
            if (phase_ != StructurePhase::DARK_AGES) {
                depositBubbleKernel(ionization, ionization_weight,
                                    {p.x[i], p.y[i], p.z[i]},
                                    (p.type[i] == ParticleType::BLACKHOLE) ? 3.4f : 2.6f,
                                    source_strength * 0.32f,
                                    source_strength * 0.18f);
            }
        }

        if (phase_ == StructurePhase::REIONIZATION && (p.type[i] == ParticleType::STAR || p.type[i] == ParticleType::BLACKHOLE)) {
            const glm::dvec3 front_dir = preferredFrontDirection(p, i, phase_);
            const float source_strength = (p.type[i] == ParticleType::BLACKHOLE ? 1.1f : 0.7f) * ionization_force;
            depositFrontKernel({p.x[i], p.y[i], p.z[i]}, front_dir,
                               source_strength,
                               p.type[i] == ParticleType::BLACKHOLE ? 6 : 5,
                               0.45f + 0.20f * front_anisotropy,
                               0.52f / std::max(front_anisotropy, 0.25f));
        }
    }

    for (const Halo& halo : halos_) {
        float halo_strength = std::clamp(static_cast<float>(halo.member_count) / 24.0f, 0.2f, 2.8f);
        float local_ionization = (phase_ == StructurePhase::REIONIZATION)
            ? halo_strength * (0.22f + 0.08f * static_cast<float>(halo.emissivity))
            : halo_strength * 0.10f;
        depositKernel({halo.cx, halo.cy, halo.cz}, {0.0, 0.0, 0.0},
                      halo_strength, (phase_ == StructurePhase::MATURE) ? 4 : 3, 0.26f, local_ionization);
        depositScalarKernel(emissivity, emissivity_weight,
                            {halo.cx, halo.cy, halo.cz},
                            static_cast<float>(halo.emissivity) * 0.22f,
                            (phase_ == StructurePhase::REIONIZATION) ? 4 : 3,
                            0.24f);
        if (phase_ != StructurePhase::DARK_AGES) {
            depositBubbleKernel(ionization, ionization_weight,
                                {halo.cx, halo.cy, halo.cz},
                                halo.ionized_radius * static_cast<float>(N) / static_cast<float>(box_size),
                                static_cast<float>(halo.emissivity) * 0.18f * ionization_force,
                                static_cast<float>(halo.emissivity) * 0.12f);
        }
        if (phase_ == StructurePhase::REIONIZATION) {
            const glm::dvec3 halo_dir = safeNormalize(glm::dvec3(halo.cx, halo.cy, halo.cz), glm::dvec3(0.0, 0.0, 1.0));
            depositFrontKernel({halo.cx, halo.cy, halo.cz}, halo_dir,
                               halo_strength * 0.45f * ionization_force,
                               5, 0.52f + 0.16f * front_anisotropy,
                               0.45f / std::max(front_anisotropy, 0.25f));
        }
    }

    diffusePeriodicField(emissivity, 1, 0.62f, 0.015f);
    diffusePeriodicField(ionization, (phase_ == StructurePhase::REIONIZATION) ? 2 : 1, 0.58f, 0.018f);

    const float base_floor = (phase_ == StructurePhase::DARK_AGES) ? 0.020f : (phase_ == StructurePhase::REIONIZATION ? 0.015f : 0.010f);
    const float field_gain = (phase_ == StructurePhase::DARK_AGES) ? 0.14f : 0.08f;
    for (size_t idx = 0; idx < field.data.size(); ++idx) {
        float w = std::max(weight[idx], 1e-4f);
        vx.data[idx] /= w;
        vy.data[idx] /= w;
        vz.data[idx] /= w;
        const float iw = std::max(ionization_weight[idx], 1e-4f);
        const float ew = std::max(emissivity_weight[idx], 1e-4f);
        emissivity.data[idx] = std::clamp(emissivity.data[idx] / ew * 0.12f, 0.0f, 1.0f);
        float ionized = ionization.data[idx] / iw * 0.20f + emissivity.data[idx] * 0.45f;
        if (phase_ == StructurePhase::DARK_AGES) {
            ionized = std::clamp(ionized * 0.16f, 0.0f, 0.18f);
        } else if (phase_ == StructurePhase::REIONIZATION) {
            ionized = std::clamp(ionized, 0.0f, 1.0f);
        } else {
            ionized = std::clamp(0.38f + ionized * 0.90f, 0.0f, 1.0f);
        }
        ionization.data[idx] = ionized;
        float micro = phaseWave(cosmic_time, static_cast<double>(idx) * 0.013, 4.5e-16) - 0.5f;
        const float density_mix = 1.0f - ionized * ((phase_ == StructurePhase::REIONIZATION) ? 0.14f : 0.08f);
        field.data[idx] = std::max(base_floor,
                                   field.data[idx] * field_gain * density_mix
                                   + emissivity.data[idx] * 0.045f
                                   + ionized * ((phase_ == StructurePhase::REIONIZATION) ? 0.020f : 0.010f)
                                   + micro * base_floor * 0.4f
                                   + base_floor);
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

        const float local_ionization = sampleGridWrapped(universe.ionization_field,
                                                         p.x[i], p.y[i], p.z[i],
                                                         RegimeConfig::STRUCT_BOX_SIZE_MPC);
        const float local_emissivity = sampleGridWrapped(universe.emissivity_field,
                                                         p.x[i], p.y[i], p.z[i],
                                                         RegimeConfig::STRUCT_BOX_SIZE_MPC);
        if (phase_ == StructurePhase::REIONIZATION && local_ionization > 0.92f && local_emissivity < 0.10f) {
            continue;
        }

        int neighbors = 0;
        const int required_neighbors = min_neigh + ((phase_ == StructurePhase::REIONIZATION && local_ionization > 0.65f) ? 2 : 0);
        for (size_t j = 0; j < n; ++j) {
            if (i == j || !(p.flags[j] & PF_ACTIVE)) continue;
            double dx = p.x[j]-p.x[i], dy = p.y[j]-p.y[i], dz = p.z[j]-p.z[i];
            if (dx*dx + dy*dy + dz*dz < r2_thresh) {
                if (++neighbors >= required_neighbors) break;  // encontrou o suficiente
            }
        }
        if (neighbors < required_neighbors) continue;

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
            p.color_r[i] = 1.0f; p.color_g[i] = 0.84f; p.color_b[i] = 0.60f;
            p.luminosity[i] = std::max(2.2f, 1.8f + local_emissivity * 1.4f);
        } else {
            p.color_r[i] = 1.0f; p.color_g[i] = 0.90f; p.color_b[i] = 0.72f;
            p.luminosity[i] = std::max(1.0f, 0.8f + local_emissivity * 0.7f);
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
        switch (p.type[i]) {
            case ParticleType::GAS:
                h.gas_mass += p.mass[i];
                break;
            case ParticleType::STAR:
                h.star_mass += p.mass[i];
                break;
            case ParticleType::BLACKHOLE:
                h.black_hole_mass += p.mass[i];
                break;
            default:
                break;
        }
    }

    std::vector<double> radius2_sum(static_cast<size_t>(group_id), 0.0);
    std::vector<double> max_radius2(static_cast<size_t>(group_id), 0.0);
    for (auto& h : new_halos) {
        if (h.mass > 0.0) {
            h.cx /= h.mass;
            h.cy /= h.mass;
            h.cz /= h.mass;
        }
    }

    for (size_t i = 0; i < n; ++i) {
        if (group[i] < 0 || !(p.flags[i] & PF_ACTIVE)) continue;
        const std::size_t halo_index = static_cast<std::size_t>(group[i]);
        const Halo& h = new_halos[halo_index];
        const double dx = p.x[i] - h.cx;
        const double dy = p.y[i] - h.cy;
        const double dz = p.z[i] - h.cz;
        const double r2 = dx * dx + dy * dy + dz * dz;
        radius2_sum[halo_index] += r2;
        max_radius2[halo_index] = std::max(max_radius2[halo_index], r2);
    }

    for (std::size_t halo_index = 0; halo_index < new_halos.size(); ++halo_index) {
        auto& h = new_halos[halo_index];
        if (h.mass > 0.0 && h.member_count >= 8) {
            const double rms_radius = std::sqrt(radius2_sum[halo_index] / static_cast<double>(std::max(h.member_count, 1)));
            const double enclosing_radius = std::sqrt(max_radius2[halo_index]);
            h.virial_radius = std::clamp(std::max(rms_radius * 2.2, enclosing_radius * 0.68), 0.28, 6.5);
            h.gas_fraction = static_cast<float>(std::clamp(h.gas_mass / std::max(h.mass, 1.0), 0.0, 1.0));
            h.stellar_fraction = static_cast<float>(std::clamp(h.star_mass / std::max(h.mass, 1.0), 0.0, 1.0));
            const double source_units = h.star_mass / std::max(RegimeConfig::MASS_STAR, 1.0)
                                      + h.black_hole_mass / std::max(RegimeConfig::MASS_BLACKHOLE * 0.65, 1.0);
            const double member_boost = std::log1p(static_cast<double>(h.member_count));
            const double phase_gain = (phase_ == StructurePhase::REIONIZATION) ? 1.0 : (phase_ == StructurePhase::MATURE ? 0.72 : 0.22);
            h.emissivity = std::clamp(std::log1p(source_units) * (1.3 + 0.25 * member_boost) * phase_gain, 0.0, 12.0);
            h.ionized_radius = static_cast<float>(std::clamp(h.virial_radius * (1.15 + 0.18 * h.emissivity), 0.35, 9.0));
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

    applyRadiativeFeedback(universe, total_visual_dt);

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
        halo_info.push_back({h.cx, h.cy, h.cz, h.mass, h.virial_radius, h.emissivity, h.gas_fraction, h.member_count});
    renderer.renderGalaxyHalos(halo_info.data(), static_cast<int>(halo_info.size()));
}
