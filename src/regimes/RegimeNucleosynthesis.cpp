// src/regimes/RegimeNucleosynthesis.cpp — Regime 2: Nucleossíntese do Big Bang (BBN)
#include "RegimeNucleosynthesis.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/Universe.hpp"
#include "../render/Renderer.hpp"
#include "../physics/NuclearNetwork.hpp"
#include "../physics/FluidGrid.hpp"
#include "../physics/Constants.hpp"
#include "../physics/Friedmann.hpp"
#include "../physics/Hadronization.hpp"
#include "../physics/ParticlePool.hpp"
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace {

glm::dvec3 safeNormalize(const glm::dvec3& v, const glm::dvec3& fallback) {
    double len2 = glm::dot(v, v);
    if (len2 <= 1e-12 || !std::isfinite(len2)) return fallback;
    return v / std::sqrt(len2);
}

glm::dvec3 orthogonalTangent(const glm::dvec3& dir, double phase) {
    glm::dvec3 ref = safeNormalize(
        glm::dvec3(std::sin(phase * 0.71 + 0.4),
                   std::cos(phase * 1.09 + 1.1),
                   std::sin(phase * 1.33 + 2.4)),
        glm::dvec3(0.577, 0.577, 0.577));
    if (std::abs(glm::dot(ref, dir)) > 0.92) {
        ref = safeNormalize(glm::dvec3(-dir.z, dir.x, dir.y), glm::dvec3(0.0, 0.0, 1.0));
    }
    return safeNormalize(glm::cross(dir, ref), glm::dvec3(0.0, 0.0, 1.0));
}

long long encodeCell(int x, int y, int z) {
    constexpr long long bias = 2048;
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

void applyLocalNuclearForces(ParticlePool& p, double visual_dt) {
    if (visual_dt <= 0.0 || p.x.empty() || p.x.size() > 14000) return;

    constexpr double kCellSize = 0.11;
    constexpr double kMaxInteractionR2 = 0.030;
    std::unordered_map<long long, std::vector<size_t>> cells;
    cells.reserve(p.x.size());

    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        if (!chemistry::isLightNucleus(p.type[i])) continue;
        int cx = static_cast<int>(std::floor(p.x[i] / kCellSize));
        int cy = static_cast<int>(std::floor(p.y[i] / kCellSize));
        int cz = static_cast<int>(std::floor(p.z[i] / kCellSize));
        cells[encodeCell(cx, cy, cz)].push_back(i);
    }

    for (const auto& entry : cells) {
        const auto& bucket = entry.second;
        for (size_t local = 0; local < bucket.size(); ++local) {
            size_t i = bucket[local];
            int cx = static_cast<int>(std::floor(p.x[i] / kCellSize));
            int cy = static_cast<int>(std::floor(p.y[i] / kCellSize));
            int cz = static_cast<int>(std::floor(p.z[i] / kCellSize));

            for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                auto it = cells.find(encodeCell(cx + dx, cy + dy, cz + dz));
                if (it == cells.end()) continue;

                for (size_t j : it->second) {
                    if (j <= i) continue;

                    double rx = p.x[j] - p.x[i];
                    double ry = p.y[j] - p.y[i];
                    double rz = p.z[j] - p.z[i];
                    double r2 = rx * rx + ry * ry + rz * rz;
                    if (r2 <= 1e-8 || r2 > kMaxInteractionR2) continue;

                    double r = std::sqrt(r2);
                    int bi = chemistry::baryonNumber(p.type[i]);
                    int bj = chemistry::baryonNumber(p.type[j]);
                    double nuclear_range = 0.028 + 0.004 * static_cast<double>(std::min(bi + bj, 8));
                    double nuclear = std::exp(-r / nuclear_range) * (0.0035 + 0.0006 * static_cast<double>(bi + bj));
                    double coulomb = std::max(0.0f, p.charge[i]) * std::max(0.0f, p.charge[j]) * 0.0008 / (r2 + 0.0025);
                    double force = (nuclear - coulomb) * visual_dt;

                    p.vx[i] -= rx * force;
                    p.vy[i] -= ry * force;
                    p.vz[i] -= rz * force;
                    p.vx[j] += rx * force;
                    p.vy[j] += ry * force;
                    p.vz[j] += rz * force;
                }
            }
        }
    }
}

}

static NuclearNetwork bbn_network;

void RegimeNucleosynthesis::onEnter(Universe& state) {
    prev_scale_factor_ = state.scale_factor;
    // Herdar composição do regime anterior quando ela já existe.
    NuclearAbundances inherited = chemistry::inferAbundances(state.particles);
    double inherited_total = inherited.Xp + inherited.Xn + inherited.Xd + inherited.Xhe3 + inherited.Xhe4 + inherited.Xli7;
    if (inherited_total > 0.0) state.abundances = inherited;
    else state.abundances = NuclearNetwork::equilibriumAbundances(state.temperature_keV);
    total_baryon_density_ = FluidGrid::baryonDensity(state.scale_factor);

    // Sincroniza pool de partículas: converte prótons/nêtrons do Regime 1
    // Partículas já definidas por RegimeManager::buildInitialState
}

void RegimeNucleosynthesis::onExit() {}

void RegimeNucleosynthesis::update(double cosmic_dt, double scale_factor, double temp_keV,
                                    Universe& universe)
{
    double a_prev_frame = std::max(prev_scale_factor_, 1e-60);
    double a_new = std::max(scale_factor, 1e-60);
    constexpr double regime_duration = CosmicClock::REGIME_START_TIMES[3] - CosmicClock::REGIME_START_TIMES[2];
    double progress_dt = (regime_duration > 0.0) ? cosmic_dt / regime_duration : 0.0;
    double total_visual_dt = cosmic_dt <= 0.0 ? 0.0
                                               : std::clamp(progress_dt * 24.0, 0.001, 0.04);
    int substeps = computeSubsteps(total_visual_dt, 0.006, 8);
    double sub_cosmic_dt = cosmic_dt / static_cast<double>(substeps);
    double sub_visual_dt = total_visual_dt / static_cast<double>(substeps);

    // Atualiza as cores visuais das partículas com base nas abundâncias
    // Reatribui tipos ao longo do tempo conforme He4 se forma
    ParticlePool& p = universe.particles;
    size_t n = p.x.size();
    for (int step = 0; step < substeps; ++step) {
        double alpha1 = static_cast<double>(step + 1) / static_cast<double>(substeps);
        double a_step = interpolatePositive(a_prev_frame, a_new, alpha1);
        double sub_temp_keV = phys::temperature_keV_from_scale(a_step);

        // Avança a rede de reações nucleares com maior resolução temporal.
        bbn_network.step(universe.abundances, sub_cosmic_dt, sub_temp_keV, a_step);

        if (!(n > 0 && n < 20000)) {
            continue;
        }

        applyLocalNuclearForces(p, sub_visual_dt);

        glm::dvec3 active_center(0.0);
        size_t active_count = 0;
        for (size_t i = 0; i < n; ++i) {
            if (!(p.flags[i] & PF_ACTIVE)) continue;
            active_center += glm::dvec3(p.x[i], p.y[i], p.z[i]);
            ++active_count;
        }
        if (active_count > 0) {
            active_center /= static_cast<double>(active_count);
        }

        double total = universe.abundances.Xp + universe.abundances.Xn
                     + universe.abundances.Xd + universe.abundances.Xhe3
                     + universe.abundances.Xhe4 + universe.abundances.Xli7;
        if (total > 0.0) {
            // Fracções para cada tipo
            double fp  = universe.abundances.Xp  / total;
            double fn  = universe.abundances.Xn  / total;
            double fd  = universe.abundances.Xd  / total;
            double fhe3 = universe.abundances.Xhe3 / total;
            double fhe4 = universe.abundances.Xhe4 / total;
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
                else if (frac < fp + fn + fd)          new_type = ParticleType::DEUTERIUM;
                else if (frac < fp + fn + fd + fhe3)   new_type = ParticleType::HELIUM3;
                else if (frac < fp + fn + fd + fhe3 + fhe4) new_type = ParticleType::HELIUM4NUCLEI;
                else                                   new_type = ParticleType::LITHIUM7;
                p.type[i] = new_type;
                ParticlePool::defaultColor(new_type, p.color_r[i], p.color_g[i], p.color_b[i]);
                p.mass[i] = chemistry::restMass(new_type);
                p.charge[i] = static_cast<float>(chemistry::atomicCharge(new_type));
                // He4 é mais brilhante (recém formado)
                p.luminosity[i] = (new_type == ParticleType::HELIUM4NUCLEI || new_type == ParticleType::HELIUM3 || new_type == ParticleType::LITHIUM7) ? 2.0f : 1.0f;

                if (sub_visual_dt > 0.0) {
                    double phase = universe.cosmic_time * 0.05 + static_cast<double>(i) * 0.13;
                    double jitter = (new_type == ParticleType::HELIUM4NUCLEI || new_type == ParticleType::HELIUM3 || new_type == ParticleType::LITHIUM7) ? 0.006 : 0.003;
                    double dx = p.x[i] - active_center.x;
                    double dy = p.y[i] - active_center.y;
                    double dz = p.z[i] - active_center.z;
                    glm::dvec3 radial = safeNormalize({dx, dy, dz},
                        glm::dvec3(std::cos(phase), std::sin(phase * 0.7), std::cos(phase * 1.3)));
                    glm::dvec3 tangent = orthogonalTangent(radial, phase);
                    double radial_drive = std::sin(phase * 0.73) * jitter * 0.8;
                    double tangential_drive = std::cos(phase * 1.11) * jitter * 0.6;
                    p.vx[i] = p.vx[i] * 0.985 + radial.x * radial_drive + tangent.x * tangential_drive;
                    p.vy[i] = p.vy[i] * 0.985 + radial.y * radial_drive + tangent.y * tangential_drive;
                    p.vz[i] = p.vz[i] * 0.985 + radial.z * radial_drive + tangent.z * tangential_drive;

                    double collapse = (new_type == ParticleType::HELIUM4NUCLEI || new_type == ParticleType::HELIUM3 || new_type == ParticleType::LITHIUM7 || new_type == ParticleType::DEUTERIUM) ? 0.02 : 0.008;
                    p.vx[i] += -dx * collapse * sub_visual_dt;
                    p.vy[i] += -dy * collapse * sub_visual_dt;
                    p.vz[i] += -dz * collapse * sub_visual_dt;
                    p.x[i] += p.vx[i] * sub_visual_dt;
                    p.y[i] += p.vy[i] * sub_visual_dt;
                    p.z[i] += p.vz[i] * sub_visual_dt;
                }
            }
        }
    }

    total_baryon_density_ = FluidGrid::baryonDensity(scale_factor);
    prev_scale_factor_ = a_new;

    // Expande posições com o universo
    // (tratado no tick do RegimeManager via razão do fator de escala)
    universe.scale_factor    = scale_factor;
    universe.temperature_keV = temp_keV;
}

void RegimeNucleosynthesis::render(Renderer& renderer, const Universe& universe) {
    renderer.renderParticles(universe);
    renderer.renderNuclearAbundances(universe.abundances);
}
