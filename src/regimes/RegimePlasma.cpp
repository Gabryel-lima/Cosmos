// src/regimes/RegimePlasma.cpp — Regime 3: Plasma de Fótons / Recombinação
#include "RegimePlasma.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/SimulationRandom.hpp"
#include "../core/Universe.hpp"
#include "../render/Renderer.hpp"
#include "../physics/FluidGrid.hpp"
#include "../physics/Constants.hpp"
#include "../physics/Hadronization.hpp"
#include "../physics/ParticlePool.hpp"
#include "../physics/Friedmann.hpp"
#include <random>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace {

FluidGrid fluid_solver;

std::mt19937& plasmaRng() {
    static std::mt19937 rng = simrng::makeStream("plasma");
    return rng;
}

bool isLightReactiveMatter(ParticleType type) {
    return type == ParticleType::GAS || chemistry::isLightNucleus(type);
}

double interactionScaleForParticle(const ParticlePool& particles, size_t index) {
    double scale = 1.0;
    int baryons = std::max(1, chemistry::baryonNumber(particles.type[index]));
    scale += 0.10 * static_cast<double>(baryons - 1);
    if ((particles.flags[index] & PF_BOUND) != 0u) scale += 0.18;
    if (particles.type[index] == ParticleType::GAS) scale += 0.22;
    if (particles.type[index] == ParticleType::ELECTRON) scale *= 0.82;
    if (particles.type[index] == ParticleType::PHOTON) scale *= 0.92;
    return scale;
}

glm::dvec3 safeNormalize(const glm::dvec3& v, const glm::dvec3& fallback) {
    double len2 = glm::dot(v, v);
    if (len2 <= 1e-12 || !std::isfinite(len2)) return fallback;
    return v / std::sqrt(len2);
}

glm::dvec3 orthogonalTangent(const glm::dvec3& dir, double phase) {
    glm::dvec3 ref = safeNormalize(
        glm::dvec3(std::sin(phase * RegimeConfig::PLASMA_WAVE_TANGENT_PHASE_A + RegimeConfig::PLASMA_WAVE_TANGENT_OFFSET_A),
                   std::cos(phase * RegimeConfig::PLASMA_WAVE_TANGENT_PHASE_B + RegimeConfig::PLASMA_WAVE_TANGENT_OFFSET_B),
                   std::sin(phase * RegimeConfig::PLASMA_WAVE_TANGENT_PHASE_C + RegimeConfig::PLASMA_WAVE_TANGENT_OFFSET_C)),
        glm::dvec3(RegimeConfig::PLASMA_WAVE_FALLBACK_AXIS,
                   RegimeConfig::PLASMA_WAVE_FALLBACK_AXIS,
                   RegimeConfig::PLASMA_WAVE_FALLBACK_AXIS));
    if (std::abs(glm::dot(ref, dir)) > RegimeConfig::PLASMA_WAVE_REORTHOGONALIZE_DOT) {
        ref = safeNormalize(glm::dvec3(-dir.z, dir.x, dir.y), glm::dvec3(0.0, 0.0, 1.0));
    }
    return safeNormalize(glm::cross(dir, ref), glm::dvec3(0.0, 0.0, 1.0));
}

long long encodeCell(int x, int y, int z) {
    constexpr long long bias = 4096;
    return ((static_cast<long long>(x) + bias) << 42)
         ^ ((static_cast<long long>(y) + bias) << 21)
         ^  (static_cast<long long>(z) + bias);
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

double recombinationVisibilityPulse(double ionized_fraction, double neutral_fraction, float width_multiplier) {
    const double center = RegimeConfig::PLASMA_RECOMBINATION_TRIGGER_XE * 0.92;
    const double sigma = std::max(0.015, 0.045 * static_cast<double>(std::clamp(width_multiplier, 0.35f, 2.5f)));
    const double delta = (ionized_fraction - center) / sigma;
    const double gaussian = std::exp(-delta * delta);
    return gaussian * std::clamp(0.35 + neutral_fraction * 1.15, 0.0, 1.4);
}

ParticleType normalizedFusionType(ParticleType type) {
    return (type == ParticleType::GAS) ? ParticleType::PROTON : type;
}

bool isNeutralAtom(const ParticlePool& particles, size_t index) {
    return particles.type[index] == ParticleType::GAS ||
           (((particles.flags[index] & PF_BOUND) != 0u) && particles.charge[index] <= 0.05f);
}

bool isPhotonAbsorbableState(const ParticlePool& particles, size_t index) {
    return isNeutralAtom(particles, index) ||
           (chemistry::isLightNucleus(particles.type[index]) && particles.charge[index] > 0.0f);
}

float effectiveCharge(const ParticlePool& particles, size_t index) {
    if (!(particles.flags[index] & PF_ACTIVE)) return 0.0f;
    return particles.charge[index];
}

void applyNeutralVisual(ParticlePool& particles, size_t index) {
    float gr, gg, gb;
    ParticlePool::defaultColor(ParticleType::GAS, gr, gg, gb);
    if (particles.type[index] == ParticleType::GAS) {
        particles.color_r[index] = gr;
        particles.color_g[index] = gg;
        particles.color_b[index] = gb;
    } else {
        float nr, ng, nb;
        ParticlePool::defaultColor(particles.type[index], nr, ng, nb);
        particles.color_r[index] = nr * 0.35f + gr * 0.65f;
        particles.color_g[index] = ng * 0.35f + gg * 0.65f;
        particles.color_b[index] = nb * 0.35f + gb * 0.65f;
    }
    particles.luminosity[index] = std::clamp(particles.luminosity[index] * 0.85f + 0.15f, 0.35f, 1.4f);
}

void setIonizedVisual(ParticlePool& particles, size_t index) {
    ParticlePool::defaultColor(particles.type[index],
                               particles.color_r[index],
                               particles.color_g[index],
                               particles.color_b[index]);
    particles.luminosity[index] = std::max(particles.luminosity[index], 1.0f);
}

void makeNeutralAtom(ParticlePool& particles, size_t index) {
    if (particles.type[index] == ParticleType::PROTON) {
        particles.type[index] = ParticleType::GAS;
        particles.mass[index] = chemistry::restMass(ParticleType::PROTON);
    }
    particles.charge[index] = 0.0f;
    particles.flags[index] |= PF_BOUND;
    applyNeutralVisual(particles, index);
}

void ionizeMatter(ParticlePool& particles, size_t index) {
    if (particles.type[index] == ParticleType::GAS) {
        particles.type[index] = ParticleType::PROTON;
        particles.mass[index] = chemistry::restMass(ParticleType::PROTON);
        particles.charge[index] = 1.0f;
    } else {
        particles.charge[index] = std::max(1.0f, static_cast<float>(chemistry::atomicCharge(particles.type[index])));
    }
    particles.flags[index] &= ~PF_BOUND;
    setIonizedVisual(particles, index);
}

void randomDirection(double& dx, double& dy, double& dz, std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    do {
        dx = dist(rng);
        dy = dist(rng);
        dz = dist(rng);
    } while (dx * dx + dy * dy + dz * dz < 1e-6);
    double inv_len = 1.0 / std::sqrt(dx * dx + dy * dy + dz * dz);
    dx *= inv_len;
    dy *= inv_len;
    dz *= inv_len;
}

void emitPhoton(ParticlePool& particles, size_t source_index, std::mt19937& rng,
                float luminosity, double speed_scale, int& photons_left)
{
    if (photons_left <= 0) return;
    double dx, dy, dz;
    randomDirection(dx, dy, dz, rng);
    std::uniform_real_distribution<double> jitter(-0.015, 0.015);
    float r, g, b;
    ParticlePool::defaultColor(ParticleType::PHOTON, r, g, b);
    size_t photon = particles.add(particles.x[source_index] + jitter(rng),
                                  particles.y[source_index] + jitter(rng),
                                  particles.z[source_index] + jitter(rng),
                                  particles.vx[source_index] + dx * speed_scale,
                                  particles.vy[source_index] + dy * speed_scale,
                                  particles.vz[source_index] + dz * speed_scale,
                                  0.0, ParticleType::PHOTON, r, g, b, 0.0f);
    particles.luminosity[photon] = luminosity;
    --photons_left;
}

void emitElectron(ParticlePool& particles, size_t source_index, std::mt19937& rng,
                  int& electron_budget)
{
    if (electron_budget <= 0) return;
    double dx, dy, dz;
    randomDirection(dx, dy, dz, rng);
    std::uniform_real_distribution<double> jitter(-0.01, 0.01);
    float r, g, b;
    ParticlePool::defaultColor(ParticleType::ELECTRON, r, g, b);
    size_t electron = particles.add(particles.x[source_index] + jitter(rng),
                                    particles.y[source_index] + jitter(rng),
                                    particles.z[source_index] + jitter(rng),
                                    particles.vx[source_index] + dx * RegimeConfig::PLASMA_ELECTRON_EMISSION_SPEED,
                                    particles.vy[source_index] + dy * RegimeConfig::PLASMA_ELECTRON_EMISSION_SPEED,
                                    particles.vz[source_index] + dz * RegimeConfig::PLASMA_ELECTRON_EMISSION_SPEED,
                                    phys::m_e, ParticleType::ELECTRON, r, g, b, -1.0f);
    particles.luminosity[electron] = RegimeConfig::PLASMA_ELECTRON_EMISSION_LUMINOSITY;
    --electron_budget;
}

void emitNeutrino(ParticlePool& particles, size_t source_index, std::mt19937& rng) {
    double dx, dy, dz;
    randomDirection(dx, dy, dz, rng);
    std::uniform_real_distribution<double> jitter(-0.01, 0.01);
    float r, g, b;
    ParticlePool::defaultColor(ParticleType::NEUTRINO, r, g, b);
    size_t neutrino = particles.add(particles.x[source_index] + jitter(rng),
                                    particles.y[source_index] + jitter(rng),
                                    particles.z[source_index] + jitter(rng),
                                    particles.vx[source_index] + dx * RegimeConfig::PLASMA_NEUTRINO_EMISSION_SPEED,
                                    particles.vy[source_index] + dy * RegimeConfig::PLASMA_NEUTRINO_EMISSION_SPEED,
                                    particles.vz[source_index] + dz * RegimeConfig::PLASMA_NEUTRINO_EMISSION_SPEED,
                                    0.0, ParticleType::NEUTRINO, r, g, b, 0.0f);
    particles.luminosity[neutrino] = RegimeConfig::PLASMA_NEUTRINO_EMISSION_LUMINOSITY;
}

ParticleType fusionProduct(ParticleType a, ParticleType b) {
    a = normalizedFusionType(a);
    b = normalizedFusionType(b);
    if ((a == ParticleType::PROTON && b == ParticleType::NEUTRON) ||
        (a == ParticleType::NEUTRON && b == ParticleType::PROTON)) return ParticleType::DEUTERIUM;
    if ((a == ParticleType::DEUTERIUM && b == ParticleType::PROTON) ||
        (a == ParticleType::PROTON && b == ParticleType::DEUTERIUM)) return ParticleType::HELIUM3;
    if ((a == ParticleType::DEUTERIUM && b == ParticleType::DEUTERIUM) ||
        (a == ParticleType::HELIUM3 && b == ParticleType::NEUTRON) ||
        (a == ParticleType::NEUTRON && b == ParticleType::HELIUM3) ||
        (a == ParticleType::HELIUM3 && b == ParticleType::DEUTERIUM) ||
        (a == ParticleType::DEUTERIUM && b == ParticleType::HELIUM3)) return ParticleType::HELIUM4NUCLEI;
    if ((a == ParticleType::HELIUM4NUCLEI && b == ParticleType::HELIUM3) ||
        (a == ParticleType::HELIUM3 && b == ParticleType::HELIUM4NUCLEI) ||
        (a == ParticleType::HELIUM4NUCLEI && b == ParticleType::DEUTERIUM) ||
        (a == ParticleType::DEUTERIUM && b == ParticleType::HELIUM4NUCLEI)) return ParticleType::LITHIUM7;
    return ParticleType::COUNT;
}

double fusionProbability(ParticleType product, double temp_keV, double dt) {
    const double thermal = std::clamp(temp_keV / 0.07, 0.02, 1.0);
    double base = RegimeConfig::plasmaFusionBaseProbability(product);
    if (base <= 0.0) return 0.0;
    return std::clamp(base * dt * (0.5 + thermal), 0.0, 0.35);
}

void fuseParticles(ParticlePool& particles, size_t keep, size_t consume,
                   ParticleType product, int& photons_left)
{
    particles.x[keep] = 0.5 * (particles.x[keep] + particles.x[consume]);
    particles.y[keep] = 0.5 * (particles.y[keep] + particles.y[consume]);
    particles.z[keep] = 0.5 * (particles.z[keep] + particles.z[consume]);
    particles.vx[keep] = 0.5 * (particles.vx[keep] + particles.vx[consume]);
    particles.vy[keep] = 0.5 * (particles.vy[keep] + particles.vy[consume]);
    particles.vz[keep] = 0.5 * (particles.vz[keep] + particles.vz[consume]);
    particles.type[keep] = product;
    particles.mass[keep] = chemistry::restMass(product);
    particles.charge[keep] = static_cast<float>(chemistry::atomicCharge(product));
    particles.flags[keep] &= ~PF_BOUND;
    ParticlePool::defaultColor(product,
                               particles.color_r[keep],
                               particles.color_g[keep],
                               particles.color_b[keep]);
    particles.luminosity[keep] = std::min(RegimeConfig::PLASMA_FUSION_LUMINOSITY_MAX,
                                          particles.luminosity[keep] + RegimeConfig::PLASMA_FUSION_LUMINOSITY_BOOST);
    particles.temp_particle[keep] += RegimeConfig::PLASMA_FUSION_TEMP_BOOST;
    particles.deactivate(consume);
    emitPhoton(particles, keep, plasmaRng(), RegimeConfig::PLASMA_FUSION_PHOTON_LUMINOSITY,
               RegimeConfig::PLASMA_FUSION_PHOTON_SPEED, photons_left);
}

void applyMicrophysics(Universe& universe, double visual_dt, double temp_keV, double neutral_fraction) {
    ParticlePool& particles = universe.particles;
    const size_t initial_count = particles.x.size();
    if (visual_dt <= 0.0 || initial_count == 0 || initial_count > RegimeConfig::PLASMA_MICROPHYSICS_MAX_PARTICLES) return;

    std::mt19937& rng = plasmaRng();
    std::uniform_real_distribution<double> chance(0.0, 1.0);

    int photon_budget = RegimeConfig::PLASMA_MAX_MICRO_PHOTONS;
    int fusion_budget = RegimeConfig::PLASMA_MAX_MICRO_FUSIONS;
    int decay_budget = RegimeConfig::PLASMA_MAX_MICRO_DECAYS;
    int electron_budget = RegimeConfig::PLASMA_MAX_MICRO_DECAYS + RegimeConfig::PLASMA_MAX_MICRO_FUSIONS;

    const double interaction_r2 = RegimeConfig::PLASMA_INTERACTION_RADIUS * RegimeConfig::PLASMA_INTERACTION_RADIUS;
    const double capture_r2 = RegimeConfig::PLASMA_CAPTURE_RADIUS * RegimeConfig::PLASMA_CAPTURE_RADIUS;
    const double fusion_r2 = RegimeConfig::PLASMA_FUSION_RADIUS * RegimeConfig::PLASMA_FUSION_RADIUS;
    const double photon_scatter_r2 = RegimeConfig::PLASMA_PHOTON_SCATTER_RADIUS * RegimeConfig::PLASMA_PHOTON_SCATTER_RADIUS;
    const double cell_size = RegimeConfig::PLASMA_INTERACTION_RADIUS * RegimeConfig::PLASMA_INTERACTION_CELL_SIZE_MULT;

    std::unordered_map<long long, std::vector<size_t>> cells;
    cells.reserve(initial_count);
    for (size_t i = 0; i < initial_count; ++i) {
        if (!(particles.flags[i] & PF_ACTIVE)) continue;
        int cx = static_cast<int>(std::floor(particles.x[i] / cell_size));
        int cy = static_cast<int>(std::floor(particles.y[i] / cell_size));
        int cz = static_cast<int>(std::floor(particles.z[i] / cell_size));
        cells[encodeCell(cx, cy, cz)].push_back(i);
    }

    for (size_t i = 0; i < initial_count && decay_budget > 0; ++i) {
        if (!(particles.flags[i] & PF_ACTIVE)) continue;
        if (particles.type[i] != ParticleType::NEUTRON) continue;
        const double decay_probability = std::clamp(visual_dt * RegimeConfig::PLASMA_NEUTRON_DECAY_RATE
                                * (1.0 - neutral_fraction * RegimeConfig::PLASMA_NEUTRON_DECAY_NEUTRAL_FACTOR),
                                0.0, RegimeConfig::PLASMA_NEUTRON_DECAY_MAX_PROBABILITY);
        if (chance(rng) > decay_probability) continue;

        particles.type[i] = ParticleType::PROTON;
        particles.mass[i] = chemistry::restMass(ParticleType::PROTON);
        particles.charge[i] = 1.0f;
        particles.flags[i] &= ~PF_BOUND;
        setIonizedVisual(particles, i);
        particles.temp_particle[i] += RegimeConfig::PLASMA_NEUTRON_DECAY_TEMP_BOOST;
        emitElectron(particles, i, rng, electron_budget);
        emitNeutrino(particles, i, rng);
        emitPhoton(particles, i, rng, RegimeConfig::PLASMA_NEUTRON_DECAY_PHOTON_LUMINOSITY,
               RegimeConfig::PLASMA_NEUTRON_DECAY_PHOTON_SPEED, photon_budget);
        --decay_budget;
    }

    for (size_t i = 0; i < initial_count; ++i) {
        if (!(particles.flags[i] & PF_ACTIVE)) continue;

        int cx = static_cast<int>(std::floor(particles.x[i] / cell_size));
        int cy = static_cast<int>(std::floor(particles.y[i] / cell_size));
        int cz = static_cast<int>(std::floor(particles.z[i] / cell_size));

        for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            auto bucket_it = cells.find(encodeCell(cx + dx, cy + dy, cz + dz));
            if (bucket_it == cells.end()) continue;

            for (size_t j : bucket_it->second) {
                if (j <= i || j >= initial_count) continue;
                if (!(particles.flags[j] & PF_ACTIVE)) continue;

                const double dx = particles.x[j] - particles.x[i];
                const double dy = particles.y[j] - particles.y[i];
                const double dz = particles.z[j] - particles.z[i];
                const double pair_scale = 0.5 * (interactionScaleForParticle(particles, i) + interactionScaleForParticle(particles, j));
                const double pair_interaction_r2 = interaction_r2 * pair_scale * pair_scale;
                const double pair_capture_r2 = capture_r2 * pair_scale * pair_scale;
                const double pair_fusion_r2 = fusion_r2 * pair_scale * pair_scale;
                const double pair_photon_scatter_r2 = photon_scatter_r2 * pair_scale * pair_scale;
                const double r2 = dx * dx + dy * dy + dz * dz;
                if (r2 <= 1e-8 || r2 > pair_interaction_r2) continue;

                const double inv_r = 1.0 / std::sqrt(r2 + 1e-8);
                const float qi = effectiveCharge(particles, i);
                const float qj = effectiveCharge(particles, j);

                if (std::abs(qi) > 0.01f || std::abs(qj) > 0.01f) {
                    const double scalar = (-static_cast<double>(qi) * static_cast<double>(qj))
                                        * visual_dt * RegimeConfig::PLASMA_ELECTROSTATIC_FORCE
                                        / (r2 + RegimeConfig::PLASMA_ELECTROSTATIC_SOFTENING);
                    particles.vx[i] += scalar * dx;
                    particles.vy[i] += scalar * dy;
                    particles.vz[i] += scalar * dz;
                    particles.vx[j] -= scalar * dx;
                    particles.vy[j] -= scalar * dy;
                    particles.vz[j] -= scalar * dz;
                }

                if (isNeutralAtom(particles, i) && isNeutralAtom(particles, j)) {
                    const double attraction = visual_dt * RegimeConfig::PLASMA_NEUTRAL_ATTRACTION_FORCE * inv_r;
                    particles.vx[i] += attraction * dx;
                    particles.vy[i] += attraction * dy;
                    particles.vz[i] += attraction * dz;
                    particles.vx[j] -= attraction * dx;
                    particles.vy[j] -= attraction * dy;
                    particles.vz[j] -= attraction * dz;
                }

                const bool photon_i = particles.type[i] == ParticleType::PHOTON;
                const bool photon_j = particles.type[j] == ParticleType::PHOTON;
                const bool electron_i = particles.type[i] == ParticleType::ELECTRON;
                const bool electron_j = particles.type[j] == ParticleType::ELECTRON;

                if ((photon_i && electron_j) || (photon_j && electron_i)) {
                    if (r2 < pair_photon_scatter_r2) {
                        const size_t photon_idx = photon_i ? i : j;
                        const size_t electron_idx = photon_i ? j : i;
                        const double scatter = visual_dt * RegimeConfig::PLASMA_PHOTON_ELECTRON_SCATTER_FORCE * inv_r;
                        particles.vx[electron_idx] += scatter * dx;
                        particles.vy[electron_idx] += scatter * dy;
                        particles.vz[electron_idx] += scatter * dz;
                        particles.vx[photon_idx] -= scatter * dx * RegimeConfig::PLASMA_PHOTON_ELECTRON_RECOIL_MULT;
                        particles.vy[photon_idx] -= scatter * dy * RegimeConfig::PLASMA_PHOTON_ELECTRON_RECOIL_MULT;
                        particles.vz[photon_idx] -= scatter * dz * RegimeConfig::PLASMA_PHOTON_ELECTRON_RECOIL_MULT;
                        particles.temp_particle[electron_idx] += RegimeConfig::PLASMA_PHOTON_ELECTRON_TEMP_BOOST;
                        particles.luminosity[photon_idx] = std::max(RegimeConfig::PLASMA_PHOTON_SCATTER_LUMINOSITY_FLOOR,
                                                                    particles.luminosity[photon_idx] * RegimeConfig::PLASMA_PHOTON_SCATTER_LUMINOSITY_RETAIN);
                    }
                    continue;
                }

                if ((photon_i && isPhotonAbsorbableState(particles, j)) ||
                    (photon_j && isPhotonAbsorbableState(particles, i))) {
                    if (r2 < pair_photon_scatter_r2) {
                        const size_t photon_idx = photon_i ? i : j;
                        const size_t matter_idx = photon_i ? j : i;
                        const double ionize_probability = std::clamp(visual_dt * (RegimeConfig::PLASMA_IONIZATION_BASE_RATE
                                                    + temp_keV * RegimeConfig::PLASMA_IONIZATION_TEMPERATURE_SCALE)
                                                    * (1.0 - neutral_fraction * RegimeConfig::PLASMA_IONIZATION_NEUTRAL_FACTOR),
                                                    0.0, RegimeConfig::PLASMA_IONIZATION_MAX_PROBABILITY);
                        if (chance(rng) < ionize_probability) {
                            particles.deactivate(photon_idx);
                            ionizeMatter(particles, matter_idx);
                            particles.temp_particle[matter_idx] += RegimeConfig::PLASMA_IONIZATION_TEMP_BOOST;
                            emitElectron(particles, matter_idx, rng, electron_budget);
                        }
                    }
                    continue;
                }

                if ((electron_i && isLightReactiveMatter(particles.type[j]) && qj > 0.0f) ||
                    (electron_j && isLightReactiveMatter(particles.type[i]) && qi > 0.0f)) {
                    if (r2 < pair_capture_r2) {
                        const size_t electron_idx = electron_i ? i : j;
                        const size_t nucleus_idx = electron_i ? j : i;
                                const double capture_probability = std::clamp(visual_dt * (RegimeConfig::PLASMA_CAPTURE_BASE_RATE
                                                                          + neutral_fraction * RegimeConfig::PLASMA_CAPTURE_NEUTRAL_SCALE)
                                                                          * (RegimeConfig::PLASMA_CAPTURE_TEMP_BASE
                                                                              + particles.temp_particle[nucleus_idx] * RegimeConfig::PLASMA_CAPTURE_TEMP_SCALE),
                                                                          0.0, RegimeConfig::PLASMA_CAPTURE_MAX_PROBABILITY);
                        if (chance(rng) < capture_probability) {
                            particles.deactivate(electron_idx);
                            makeNeutralAtom(particles, nucleus_idx);
                            particles.temp_particle[nucleus_idx] += RegimeConfig::PLASMA_CAPTURE_TEMP_BOOST;
                            emitPhoton(particles, nucleus_idx, rng, RegimeConfig::PLASMA_CAPTURE_PHOTON_LUMINOSITY,
                                       RegimeConfig::PLASMA_CAPTURE_PHOTON_SPEED, photon_budget);
                        }
                    }
                    continue;
                }

                if (fusion_budget > 0 && r2 < pair_fusion_r2 &&
                    isLightReactiveMatter(particles.type[i]) && isLightReactiveMatter(particles.type[j])) {
                    ParticleType product = fusionProduct(particles.type[i], particles.type[j]);
                    if (product != ParticleType::COUNT && chance(rng) < fusionProbability(product, temp_keV, visual_dt)) {
                        fuseParticles(particles, i, j, product, photon_budget);
                        --fusion_budget;
                        break;
                    }
                }
            }
        }
    }

    for (size_t i = 0; i < particles.x.size() && photon_budget > 0; ++i) {
        if (!(particles.flags[i] & PF_ACTIVE)) continue;
        if (!isNeutralAtom(particles, i)) continue;
        if (particles.temp_particle[i] < RegimeConfig::PLASMA_NEUTRAL_EMISSION_TEMP_THRESHOLD) continue;
        const double emission_probability = std::clamp(visual_dt * particles.temp_particle[i] * RegimeConfig::PLASMA_NEUTRAL_EMISSION_RATE,
                                                       0.0, RegimeConfig::PLASMA_NEUTRAL_EMISSION_MAX_PROBABILITY);
        if (chance(rng) < emission_probability) {
            particles.temp_particle[i] *= RegimeConfig::PLASMA_NEUTRAL_EMISSION_TEMP_RETAIN;
            emitPhoton(particles, i, rng, RegimeConfig::PLASMA_NEUTRAL_EMISSION_PHOTON_LUMINOSITY,
                       RegimeConfig::PLASMA_NEUTRAL_EMISSION_PHOTON_SPEED, photon_budget);
        }
    }
}

} // namespace

void RegimePlasma::onEnter(Universe& state) {
    prev_scale_factor_ = state.scale_factor;
    cmb_flash_triggered_ = false;
    cmb_flash_t_         = 0.0f;
    recombined_fraction_ = 0.0;
    wave_phase_          = 0.0f;
    baryon_density_      = FluidGrid::baryonDensity(state.scale_factor);

    // Inicializa a grade fluida se ainda não estiver definida
    int N = state.quality.grid_res;
    if (state.density_field.NX != N) {
        state.density_field.resize(N, N, N);
        state.velocity_x.resize(N, N, N);
        state.velocity_y.resize(N, N, N);
        state.velocity_z.resize(N, N, N);

        // Semeia com perturbações BAO do campo de densidade do Regime 2/3 ou aleatório
        // Usa flutuações de pequena amplitude como sementes
        std::uniform_real_distribution<float> noise(-RegimeConfig::PLASMA_BAO_NOISE_AMPLITUDE,
                                RegimeConfig::PLASMA_BAO_NOISE_AMPLITUDE);
        std::mt19937& rng = plasmaRng();
        for (int k = 0; k < N; ++k)
        for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            state.density_field.at(i, j, k) = noise(rng);
        }
    }
}

void RegimePlasma::onExit() {}

double RegimePlasma::computeIonizationFraction(double T_K, double n_b) {
    // Equação de Saha: X_e² / (1 - X_e) = (1/n_b) * (m_e kT / 2πℏ²)^(3/2) * exp(-13.6eV/kT)
    if (T_K <= 0.0 || n_b <= 0.0) return 0.0;
    double kT = phys::kB * T_K;
    double E_ion = 13.6 * phys::eV;
    double factor = std::pow(phys::m_e * kT / (2.0 * M_PI * phys::hbar * phys::hbar), 1.5);
    double saha_rhs = factor * std::exp(-E_ion / kT) / n_b;
    // X_e² = saha_rhs * (1 - X_e) → X_e = (-saha_rhs + sqrt(saha_rhs² + 4*saha_rhs)) / 2
    double X_e = (-saha_rhs + std::sqrt(saha_rhs * saha_rhs + 4.0 * saha_rhs)) / 2.0;
    return std::clamp(X_e, 0.0, 1.0);
}

void RegimePlasma::update(double cosmic_dt, double scale_factor, double temp_keV,
                           Universe& universe)
{
    double a_prev_frame = std::max(prev_scale_factor_, 1e-60);
    double a_new = std::max(scale_factor, 1e-60);
    constexpr double regime_duration = CosmicClock::REGIME_START_TIMES[6] - CosmicClock::REGIME_START_TIMES[5];
    double progress_dt = (regime_duration > 0.0) ? cosmic_dt / regime_duration : 0.0;
    ParticlePool& particles = universe.particles;
    double total_visual_dt = cosmic_dt <= 0.0 ? 0.0
                                               : std::clamp(progress_dt * RegimeConfig::PLASMA_VISUAL_GAIN,
                                                            RegimeConfig::PLASMA_VISUAL_DT_MIN,
                                                            RegimeConfig::PLASMA_VISUAL_DT_MAX);
    int substeps = computeSubsteps(total_visual_dt, RegimeConfig::PLASMA_SUBSTEP_TARGET_DT,
                                   RegimeConfig::PLASMA_MAX_SUBSTEPS);
    double sub_visual_dt = total_visual_dt / static_cast<double>(substeps);

    for (int step = 0; step < substeps; ++step) {
        double alpha1 = static_cast<double>(step + 1) / static_cast<double>(substeps);
        double a_step = interpolatePositive(a_prev_frame, a_new, alpha1);
        double sub_temp_keV = phys::temperature_keV_from_scale(a_step);
        double T_K = phys::keV_to_K(sub_temp_keV);
        double H   = phys::hubble_from_scale(a_step);
        baryon_density_ = FluidGrid::baryonDensity(a_step);
        double X_e = computeIonizationFraction(T_K, baryon_density_);
        double neutral_fraction = std::clamp(1.0 - X_e, 0.0, 1.0);
        double visibility_pulse = recombinationVisibilityPulse(X_e, neutral_fraction, universe.visual.cmb_visibility_width);
        double flash_gain = std::clamp(static_cast<double>(universe.visual.cmb_flash_strength), 0.20, 3.0);

        if (sub_visual_dt > 0.0) {
            fluid_solver.step(universe, sub_visual_dt, a_step, H, sub_temp_keV);
            applyMicrophysics(universe, sub_visual_dt, sub_temp_keV, neutral_fraction);
        }

        wave_phase_ += static_cast<float>(sub_visual_dt * RegimeConfig::PLASMA_WAVE_PHASE_SPEED);

        glm::dvec3 active_center(0.0);
        size_t active_count = 0;
        for (size_t i = 0; i < particles.x.size(); ++i) {
            if (!(particles.flags[i] & PF_ACTIVE)) continue;
            active_center += glm::dvec3(particles.x[i], particles.y[i], particles.z[i]);
            ++active_count;
        }
        if (active_count > 0) {
            active_center /= static_cast<double>(active_count);
        }

        for (size_t i = 0; i < particles.x.size(); ++i) {
            if (!(particles.flags[i] & PF_ACTIVE)) continue;

            glm::dvec3 delta = glm::dvec3(particles.x[i], particles.y[i], particles.z[i]) - active_center;
            double radius = std::sqrt(glm::dot(delta, delta));
            double phase = static_cast<double>(wave_phase_)
                         + radius * RegimeConfig::PLASMA_WAVE_RADIUS_PHASE_SCALE
                         + static_cast<double>(i) * RegimeConfig::PLASMA_WAVE_INDEX_PHASE_SCALE;
            glm::dvec3 radial = safeNormalize(delta,
                glm::dvec3(std::cos(phase), std::sin(phase * 0.7), std::cos(phase * 1.3)));
            glm::dvec3 tangent = orthogonalTangent(radial, phase);
            double swirl = std::sin(phase);
            double pulse = std::cos(phase * RegimeConfig::PLASMA_WAVE_PULSE_FREQUENCY);
            switch (particles.type[i]) {
                case ParticleType::PHOTON:
                    particles.vx[i] += (tangent.x * swirl * RegimeConfig::PLASMA_PHOTON_SWIRL_FORCE
                                      + radial.x * pulse * RegimeConfig::PLASMA_PHOTON_PULSE_FORCE) * sub_visual_dt;
                    particles.vy[i] += (tangent.y * swirl * RegimeConfig::PLASMA_PHOTON_SWIRL_FORCE
                                      + radial.y * pulse * RegimeConfig::PLASMA_PHOTON_PULSE_FORCE) * sub_visual_dt;
                    particles.vz[i] += (tangent.z * swirl * RegimeConfig::PLASMA_PHOTON_SWIRL_FORCE
                                      + radial.z * pulse * RegimeConfig::PLASMA_PHOTON_PULSE_FORCE) * sub_visual_dt;
                    particles.luminosity[i] = RegimeConfig::PLASMA_PHOTON_LUMINOSITY_BASE
                                            + RegimeConfig::PLASMA_PHOTON_LUMINOSITY_SWIRL * static_cast<float>(0.5 + 0.5 * swirl)
                                            + static_cast<float>(visibility_pulse * 1.6 * flash_gain);
                    break;
                case ParticleType::ELECTRON:
                    particles.vx[i] += (-tangent.x * swirl * RegimeConfig::PLASMA_ELECTRON_SWIRL_FORCE
                                      + radial.x * pulse * RegimeConfig::PLASMA_ELECTRON_PULSE_FORCE) * sub_visual_dt;
                    particles.vy[i] += (-tangent.y * swirl * RegimeConfig::PLASMA_ELECTRON_SWIRL_FORCE
                                      + radial.y * pulse * RegimeConfig::PLASMA_ELECTRON_PULSE_FORCE) * sub_visual_dt;
                    particles.vz[i] += (-tangent.z * swirl * RegimeConfig::PLASMA_ELECTRON_SWIRL_FORCE
                                      + radial.z * pulse * RegimeConfig::PLASMA_ELECTRON_PULSE_FORCE) * sub_visual_dt;
                    break;
                case ParticleType::PROTON:
                case ParticleType::DEUTERIUM:
                case ParticleType::HELIUM3:
                case ParticleType::HELIUM4NUCLEI:
                case ParticleType::LITHIUM7:
                case ParticleType::GAS:
                    particles.vx[i] += radial.x * swirl * sub_visual_dt * RegimeConfig::PLASMA_BARYON_SWIRL_FORCE;
                    particles.vy[i] += radial.y * swirl * sub_visual_dt * RegimeConfig::PLASMA_BARYON_SWIRL_FORCE;
                    particles.vz[i] += radial.z * swirl * sub_visual_dt * RegimeConfig::PLASMA_BARYON_SWIRL_FORCE;
                    if (particles.type[i] == ParticleType::GAS || (particles.flags[i] & PF_BOUND) != 0u) {
                        particles.luminosity[i] = std::max(particles.luminosity[i],
                                                           static_cast<float>(0.22 + visibility_pulse * 0.85));
                    }
                    break;
                default:
                    break;
            }

            particles.x[i] += particles.vx[i] * sub_visual_dt;
            particles.y[i] += particles.vy[i] * sub_visual_dt;
            particles.z[i] += particles.vz[i] * sub_visual_dt;
            particles.vx[i] *= RegimeConfig::PLASMA_PARTICLE_VELOCITY_DAMPING;
            particles.vy[i] *= RegimeConfig::PLASMA_PARTICLE_VELOCITY_DAMPING;
            particles.vz[i] *= RegimeConfig::PLASMA_PARTICLE_VELOCITY_DAMPING;
        }

        // Verifica recombinação via equação de Saha
        if ((X_e < RegimeConfig::PLASMA_RECOMBINATION_TRIGGER_XE || visibility_pulse > 0.55) && !cmb_flash_triggered_) {
            cmb_flash_triggered_ = true;
            cmb_flash_t_         = 0.0f;
        }

        if (neutral_fraction > recombined_fraction_) {
            size_t charged_nuclei = 0;
            for (size_t i = 0; i < particles.x.size(); ++i) {
                if (!(particles.flags[i] & PF_ACTIVE)) continue;
                if (isLightReactiveMatter(particles.type[i]) && particles.charge[i] > 0.05f) ++charged_nuclei;
            }

            size_t to_convert = static_cast<size_t>((neutral_fraction - recombined_fraction_) * static_cast<double>(charged_nuclei));
            size_t converted = 0;
            size_t electrons_to_hide = 0;
            int photon_budget = RegimeConfig::PLASMA_MAX_MICRO_PHOTONS / RegimeConfig::PLASMA_RECOMBINATION_PHOTON_BUDGET_DIVISOR;
            std::mt19937& rng = plasmaRng();
            for (size_t i = 0; i < particles.x.size() && converted < to_convert; ++i) {
                if (!(particles.flags[i] & PF_ACTIVE)) continue;
                int charge = static_cast<int>(std::ceil(std::max(0.0f, particles.charge[i])));
                if (!isLightReactiveMatter(particles.type[i]) || charge <= 0) continue;
                electrons_to_hide += static_cast<size_t>(charge);
                makeNeutralAtom(particles, i);
                particles.temp_particle[i] += RegimeConfig::PLASMA_RECOMBINATION_TEMP_BOOST;
                emitPhoton(particles, i, rng, RegimeConfig::PLASMA_RECOMBINATION_PHOTON_LUMINOSITY,
                           RegimeConfig::PLASMA_RECOMBINATION_PHOTON_SPEED, photon_budget);
                ++converted;
            }

            size_t hidden_electrons = 0;
            for (size_t i = 0; i < particles.x.size() && hidden_electrons < electrons_to_hide; ++i) {
                if (!(particles.flags[i] & PF_ACTIVE)) continue;
                if (particles.type[i] != ParticleType::ELECTRON) continue;
                particles.deactivate(i);
                ++hidden_electrons;
            }
            recombined_fraction_ = neutral_fraction;
        }

        if (cmb_flash_triggered_ && cmb_flash_t_ < 1.0f) {
            cmb_flash_t_ += static_cast<float>(sub_visual_dt * RegimeConfig::PLASMA_CMB_FLASH_RATE
                                             * (0.55 + visibility_pulse * flash_gain));
            cmb_flash_t_  = std::min(cmb_flash_t_, 1.0f);
        }
    }

    prev_scale_factor_ = a_new;

    universe.scale_factor    = scale_factor;
    universe.temperature_keV = temp_keV;
}

void RegimePlasma::render(Renderer& renderer, const Universe& universe) {
    renderer.renderVolumeField(universe);
    renderer.renderParticles(universe);
    if (cmb_flash_triggered_) {
        renderer.renderCMBFlash(cmb_flash_t_);
    }
}
