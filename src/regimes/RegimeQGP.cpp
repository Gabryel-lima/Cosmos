// src/regimes/RegimeQGP.cpp — Regime 1: Plasma Quark-Glúon
#include "RegimeQGP.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/SimulationRandom.hpp"
#include "../core/Universe.hpp"
#include "../render/Renderer.hpp"
#include "../physics/Constants.hpp"
#include "../physics/Friedmann.hpp"
#include "../physics/Hadronization.hpp"
#include "../physics/ParticlePool.hpp"
#include <random>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace {

std::mt19937& qgpRng() {
    static std::mt19937 rng = simrng::makeStream("qgp-regime");
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

bool isQgpCarrier(ParticleType type) {
    switch (type) {
        case ParticleType::QUARK_U:
        case ParticleType::QUARK_D:
        case ParticleType::QUARK_S:
        case ParticleType::QUARK_C:
        case ParticleType::QUARK_B:
        case ParticleType::QUARK_T:
        case ParticleType::ANTIQUARK_U:
        case ParticleType::ANTIQUARK_D:
        case ParticleType::ANTIQUARK_S:
        case ParticleType::ANTIQUARK_C:
        case ParticleType::ANTIQUARK_B:
        case ParticleType::ANTIQUARK_T:
        case ParticleType::ANTIQUARK:
        case ParticleType::GLUON:
        case ParticleType::ELECTRON:
        case ParticleType::POSITRON:
        case ParticleType::MUON:
        case ParticleType::ANTIMUON:
        case ParticleType::TAU:
        case ParticleType::ANTITAU:
        case ParticleType::PHOTON:
        case ParticleType::NEUTRINO:
        case ParticleType::NEUTRINO_E:
        case ParticleType::ANTINEUTRINO_E:
        case ParticleType::NEUTRINO_MU:
        case ParticleType::ANTINEUTRINO_MU:
        case ParticleType::NEUTRINO_TAU:
        case ParticleType::ANTINEUTRINO_TAU:
        case ParticleType::W_BOSON_POS:
        case ParticleType::W_BOSON_NEG:
        case ParticleType::Z_BOSON:
        case ParticleType::HIGGS_BOSON:
            return true;
        default:
            return false;
    }
}

bool isQgpQuark(ParticleType type) {
    switch (type) {
        case ParticleType::QUARK_U:
        case ParticleType::QUARK_D:
        case ParticleType::QUARK_S:
        case ParticleType::QUARK_C:
        case ParticleType::QUARK_B:
        case ParticleType::QUARK_T:
        case ParticleType::ANTIQUARK_U:
        case ParticleType::ANTIQUARK_D:
        case ParticleType::ANTIQUARK_S:
        case ParticleType::ANTIQUARK_C:
        case ParticleType::ANTIQUARK_B:
        case ParticleType::ANTIQUARK_T:
        case ParticleType::ANTIQUARK:
            return true;
        default:
            return false;
    }
}

bool isPrimaryColorQuark(ParticleType type) {
    return isQgpQuark(type) && type != ParticleType::ANTIQUARK_U && type != ParticleType::ANTIQUARK_D &&
           type != ParticleType::ANTIQUARK_S && type != ParticleType::ANTIQUARK_C &&
           type != ParticleType::ANTIQUARK_B && type != ParticleType::ANTIQUARK_T &&
           type != ParticleType::ANTIQUARK;
}

bool isChargedRelativistic(ParticleType type, float charge) {
    if (std::abs(charge) > 1e-4f) return true;
    return type == ParticleType::W_BOSON_POS || type == ParticleType::W_BOSON_NEG;
}

bool isShortLivedHeavySpecies(ParticleType type) {
    switch (type) {
        case ParticleType::QUARK_T:
        case ParticleType::ANTIQUARK_T:
        case ParticleType::QUARK_B:
        case ParticleType::ANTIQUARK_B:
        case ParticleType::TAU:
        case ParticleType::ANTITAU:
        case ParticleType::W_BOSON_POS:
        case ParticleType::W_BOSON_NEG:
        case ParticleType::Z_BOSON:
        case ParticleType::HIGGS_BOSON:
            return true;
        default:
            return false;
    }
}

ParticleType cooledDecayProduct(ParticleType type) {
    switch (type) {
        case ParticleType::QUARK_T:
        case ParticleType::QUARK_B:
            return ParticleType::QUARK_S;
        case ParticleType::ANTIQUARK_T:
        case ParticleType::ANTIQUARK_B:
            return ParticleType::ANTIQUARK_S;
        case ParticleType::TAU:
            return ParticleType::MUON;
        case ParticleType::ANTITAU:
            return ParticleType::ANTIMUON;
        case ParticleType::W_BOSON_POS:
            return ParticleType::POSITRON;
        case ParticleType::W_BOSON_NEG:
            return ParticleType::ELECTRON;
        case ParticleType::Z_BOSON:
        case ParticleType::HIGGS_BOSON:
            return ParticleType::PHOTON;
        default:
            return type;
    }
}

double pairCouplingScale(ParticleType a, ParticleType b) {
    const bool a_gluon = (a == ParticleType::GLUON);
    const bool b_gluon = (b == ParticleType::GLUON);
    if (a_gluon && b_gluon) return RegimeConfig::QGP_PAIR_SCALE_GLUON_GLUON;
    if (a_gluon || b_gluon) return RegimeConfig::QGP_PAIR_SCALE_MIXED_GLUON;
    return RegimeConfig::QGP_LUMINOSITY_BASELINE;
}

long long encodeCell(int x, int y, int z) {
    constexpr long long bias = 2048;
    return ((static_cast<long long>(x) + bias) << 42)
         ^ ((static_cast<long long>(y) + bias) << 21)
         ^  (static_cast<long long>(z) + bias);
}

double debyeLength(double temp_keV) {
    double t = std::clamp((temp_keV - CosmicClock::T_QGP_END) / (1.0e16 - CosmicClock::T_QGP_END), 0.0, 1.0);
    return RegimeConfig::QGP_DEBYE_LENGTH_COLD - RegimeConfig::QGP_DEBYE_LENGTH_HOT_DELTA * t;
}

void emitTravelingGluons(ParticlePool& p,
                         const std::unordered_map<long long, std::vector<size_t>>& cells,
                         double emission_radius,
                         double dt)
{
    const double emission_r2 = emission_radius * emission_radius;
    std::vector<size_t> available_gluons;
    available_gluons.reserve(p.x.size() / 4);
    for (size_t gi = 0; gi < p.x.size(); ++gi) {
        if (!(p.flags[gi] & PF_ACTIVE) || p.type[gi] != ParticleType::GLUON) continue;
        if (p.qcd_color[gi] == QcdColor::NONE && p.qcd_anticolor[gi] == QcdColor::NONE) {
            available_gluons.push_back(gi);
        }
    }
    if (available_gluons.empty()) return;

    for (size_t qi = 0; qi < p.x.size() && !available_gluons.empty(); ++qi) {
        if (!(p.flags[qi] & PF_ACTIVE) || !isPrimaryColorQuark(p.type[qi])) continue;
        if (p.qcd_color[qi] == QcdColor::NONE) continue;

        int cx = static_cast<int>(std::floor(p.x[qi] / emission_radius));
        int cy = static_cast<int>(std::floor(p.y[qi] / emission_radius));
        int cz = static_cast<int>(std::floor(p.z[qi] / emission_radius));

        size_t receiver = p.x.size();
        double receiver_d2 = emission_r2;
        QcdColor receiver_color = p.qcd_color[qi];

        for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            auto it = cells.find(encodeCell(cx + dx, cy + dy, cz + dz));
            if (it == cells.end()) continue;
            for (size_t qj : it->second) {
                if (qj == qi || !(p.flags[qj] & PF_ACTIVE) || !isPrimaryColorQuark(p.type[qj])) continue;
                if (p.qcd_color[qj] == QcdColor::NONE || p.qcd_color[qj] == p.qcd_color[qi]) continue;
                double dxq = p.x[qj] - p.x[qi];
                double dyq = p.y[qj] - p.y[qi];
                double dzq = p.z[qj] - p.z[qi];
                double d2 = dxq * dxq + dyq * dyq + dzq * dzq;
                if (d2 < receiver_d2) {
                    receiver_d2 = d2;
                    receiver = qj;
                    receiver_color = p.qcd_color[qj];
                }
            }
        }

        if (receiver >= p.x.size()) continue;

        size_t gi = available_gluons.back();
        available_gluons.pop_back();
        const QcdColor emitted_color = p.qcd_color[qi];
        p.setQcdCharge(qi, receiver_color);
        p.setQcdCharge(gi, emitted_color, qcd::antiColor(receiver_color));

        const double dx = p.x[receiver] - p.x[qi];
        const double dy = p.y[receiver] - p.y[qi];
        const double dz = p.z[receiver] - p.z[qi];
        const double inv_len = 1.0 / std::sqrt(std::max(dx * dx + dy * dy + dz * dz, 1e-8));
        p.x[gi] = p.x[qi] + dx * inv_len * RegimeConfig::QGP_GLUON_SPAWN_OFFSET;
        p.y[gi] = p.y[qi] + dy * inv_len * RegimeConfig::QGP_GLUON_SPAWN_OFFSET;
        p.z[gi] = p.z[qi] + dz * inv_len * RegimeConfig::QGP_GLUON_SPAWN_OFFSET;
        const double travel_speed = RegimeConfig::QGP_GLUON_TRAVEL_SPEED_BASE
                      + RegimeConfig::QGP_GLUON_TRAVEL_SPEED_BOOST
                      * std::min(1.0, dt * RegimeConfig::QGP_GLUON_TRAVEL_DT_SCALE);
        p.vx[gi] = p.vx[qi] + dx * inv_len * travel_speed;
        p.vy[gi] = p.vy[qi] + dy * inv_len * travel_speed;
        p.vz[gi] = p.vz[qi] + dz * inv_len * travel_speed;
        p.luminosity[qi] = std::max(p.luminosity[qi], RegimeConfig::QGP_LUMINOSITY_EMITTER_MIN);
        p.luminosity[receiver] = std::max(p.luminosity[receiver], RegimeConfig::QGP_LUMINOSITY_RECEIVER_MIN);
        p.luminosity[gi] = std::max(p.luminosity[gi], RegimeConfig::QGP_LUMINOSITY_GLUON_MIN);
    }
}

void absorbTravelingGluons(ParticlePool& p,
                          const std::unordered_map<long long, std::vector<size_t>>& cells,
                          double absorption_radius)
{
    const double absorption_r2 = absorption_radius * absorption_radius;
    for (size_t gi = 0; gi < p.x.size(); ++gi) {
        if (!(p.flags[gi] & PF_ACTIVE) || p.type[gi] != ParticleType::GLUON) continue;
        if (p.qcd_color[gi] == QcdColor::NONE || p.qcd_anticolor[gi] == QcdColor::NONE) continue;

        const QcdColor receiver_color = qcd::receiverColorFromAnticolor(p.qcd_anticolor[gi]);
        int cx = static_cast<int>(std::floor(p.x[gi] / absorption_radius));
        int cy = static_cast<int>(std::floor(p.y[gi] / absorption_radius));
        int cz = static_cast<int>(std::floor(p.z[gi] / absorption_radius));

        size_t receiver = p.x.size();
        double best_d2 = absorption_r2;
        for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            auto it = cells.find(encodeCell(cx + dx, cy + dy, cz + dz));
            if (it == cells.end()) continue;
            for (size_t qi : it->second) {
                if (!(p.flags[qi] & PF_ACTIVE) || !isPrimaryColorQuark(p.type[qi])) continue;
                if (p.qcd_color[qi] != receiver_color) continue;
                double dxq = p.x[qi] - p.x[gi];
                double dyq = p.y[qi] - p.y[gi];
                double dzq = p.z[qi] - p.z[gi];
                double d2 = dxq * dxq + dyq * dyq + dzq * dzq;
                if (d2 < best_d2) {
                    best_d2 = d2;
                    receiver = qi;
                }
            }
        }
        if (receiver >= p.x.size()) continue;

        p.setQcdCharge(receiver, p.qcd_color[gi]);
        p.luminosity[receiver] = std::max(p.luminosity[receiver], RegimeConfig::QGP_LUMINOSITY_ABSORPTION_RECEIVER_MIN);
        p.clearQcdCharge(gi);
        p.vx[gi] *= RegimeConfig::QGP_ABSORBED_GLUON_VELOCITY_RETAIN;
        p.vy[gi] *= RegimeConfig::QGP_ABSORBED_GLUON_VELOCITY_RETAIN;
        p.vz[gi] *= RegimeConfig::QGP_ABSORBED_GLUON_VELOCITY_RETAIN;
        p.luminosity[gi] = RegimeConfig::QGP_ABSORBED_GLUON_LUMINOSITY;
    }
}

void applyChargedPlasmaScattering(ParticlePool& p,
                                  const std::unordered_map<long long, std::vector<size_t>>& cells,
                                  double cutoff,
                                  double dt)
{
    const double cutoff2 = cutoff * cutoff;
    const double softening2 = 2e-4;
    const double scatter_strength = RegimeConfig::QGP_CHARGED_SCATTER_STRENGTH * dt;
    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE) || !isChargedRelativistic(p.type[i], p.charge[i])) continue;
        int cx = static_cast<int>(std::floor(p.x[i] / cutoff));
        int cy = static_cast<int>(std::floor(p.y[i] / cutoff));
        int cz = static_cast<int>(std::floor(p.z[i] / cutoff));
        for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            auto it = cells.find(encodeCell(cx + dx, cy + dy, cz + dz));
            if (it == cells.end()) continue;
            for (size_t j : it->second) {
                if (j <= i || !(p.flags[j] & PF_ACTIVE) || !isChargedRelativistic(p.type[j], p.charge[j])) continue;
                const double dxp = p.x[j] - p.x[i];
                const double dyp = p.y[j] - p.y[i];
                const double dzp = p.z[j] - p.z[i];
                const double r2 = dxp * dxp + dyp * dyp + dzp * dzp;
                if (r2 < 1e-10 || r2 > cutoff2) continue;
                const double scalar = -(static_cast<double>(p.charge[i]) * static_cast<double>(p.charge[j])) /
                                      (r2 + softening2) * scatter_strength;
                p.vx[i] += scalar * dxp;
                p.vy[i] += scalar * dyp;
                p.vz[i] += scalar * dzp;
                p.vx[j] -= scalar * dxp;
                p.vy[j] -= scalar * dyp;
                p.vz[j] -= scalar * dzp;
            }
        }
    }
}

void decayShortLivedSpecies(ParticlePool& p, int regime_index, double dt) {
    if (regime_index > 2) return;
    std::mt19937& rng = qgpRng();
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const double base_decay = (regime_index == 1)
        ? RegimeConfig::QGP_HEAVY_DECAY_RATE_REHEAT
        : RegimeConfig::QGP_HEAVY_DECAY_RATE_LEPTON;
    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE) || !isShortLivedHeavySpecies(p.type[i])) continue;
        double probability = std::clamp(base_decay * dt, 0.0, RegimeConfig::QGP_HEAVY_DECAY_MAX_PROBABILITY);
        if (unit(rng) > probability) continue;
        ParticleType cooled = cooledDecayProduct(p.type[i]);
        p.type[i] = cooled;
        p.mass[i] = chemistry::restMass(cooled);
        p.charge[i] = (cooled == ParticleType::POSITRON || cooled == ParticleType::ANTIMUON || cooled == ParticleType::ANTITAU) ? 1.0f
                    : (cooled == ParticleType::ELECTRON || cooled == ParticleType::MUON || cooled == ParticleType::TAU) ? -1.0f
                    : 0.0f;
        ParticlePool::defaultColor(cooled, p.color_r[i], p.color_g[i], p.color_b[i]);
        p.clearQcdCharge(i);
        p.luminosity[i] = std::max(RegimeConfig::QGP_HEAVY_DECAY_LUMINOSITY_FLOOR,
                       p.luminosity[i] * RegimeConfig::QGP_HEAVY_DECAY_LUMINOSITY_RETAIN);
    }
}

}

void RegimeQGP::onEnter(Universe& state) {
    prev_scale_factor_ = state.scale_factor;
    hadronized_        = false;
    // Partículas já posicionadas por RegimeManager::buildInitialState()
}

void RegimeQGP::onExit() {}

void RegimeQGP::applyScreenedCornellForces(Universe& universe, double temp_keV, double dt) {
    ParticlePool& p = universe.particles;
    size_t n = p.x.size();
    if (n == 0) return;
    const int regime_index = std::clamp(universe.regime_index, 1, 3);

    double t = std::clamp((temp_keV - CosmicClock::T_QGP_END) / (1.0e16 - CosmicClock::T_QGP_END), 0.0, 1.0);
    double alpha_s = RegimeConfig::QGP_ALPHA_S_BASE + RegimeConfig::QGP_ALPHA_S_COLD_DELTA * (1.0 - t);
    double sigma = RegimeConfig::QGP_STRING_TENSION_BASE + RegimeConfig::QGP_STRING_TENSION_COLD_DELTA * (1.0 - t);
    double rD = debyeLength(temp_keV);
    double cutoff = std::max(RegimeConfig::QGP_FORCE_CUTOFF_MIN, rD * RegimeConfig::QGP_FORCE_CUTOFF_RDEBYE_MULT);
    double cutoff2 = cutoff * cutoff;
    double softening2 = 1e-4;
    double strength = RegimeConfig::QGP_FORCE_STRENGTH * dt;

    std::unordered_map<long long, std::vector<size_t>> cells;
    cells.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE) || !isQgpCarrier(p.type[i])) continue;
        int cx = static_cast<int>(std::floor(p.x[i] / cutoff));
        int cy = static_cast<int>(std::floor(p.y[i] / cutoff));
        int cz = static_cast<int>(std::floor(p.z[i] / cutoff));
        cells[encodeCell(cx, cy, cz)].push_back(i);
    }

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE) || !isQgpCarrier(p.type[i])) continue;
        double fx = 0, fy = 0, fz = 0;

        int cx = static_cast<int>(std::floor(p.x[i] / cutoff));
        int cy = static_cast<int>(std::floor(p.y[i] / cutoff));
        int cz = static_cast<int>(std::floor(p.z[i] / cutoff));

        for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            auto it = cells.find(encodeCell(cx + dx, cy + dy, cz + dz));
            if (it == cells.end()) continue;

            for (size_t j : it->second) {
                if (j <= i || !(p.flags[j] & PF_ACTIVE) || !isQgpCarrier(p.type[j])) continue;
                double dxp = p.x[j] - p.x[i];
                double dyp = p.y[j] - p.y[i];
                double dzp = p.z[j] - p.z[i];
                double r2 = dxp * dxp + dyp * dyp + dzp * dzp;
                if (r2 < 1e-10 || r2 > cutoff2) continue;

                float factor = qcd::casimirFactor(p.qcd_color[i], p.qcd_anticolor[i],
                                                  p.qcd_color[j], p.qcd_anticolor[j]);
                if (std::abs(factor) < 1e-6f || (!isQgpQuark(p.type[i]) && p.type[i] != ParticleType::GLUON) ||
                    (!isQgpQuark(p.type[j]) && p.type[j] != ParticleType::GLUON)) continue;

                double r = std::sqrt(r2 + softening2);
                double screen = std::exp(-r / std::max(rD, 1e-3));
                double coulomb = alpha_s * screen / (r2 + softening2);
                double confinement = sigma * (1.0 - t) * std::exp(-r / std::max(rD * 1.5, 1e-3));
                double kernel = (coulomb + confinement) * pairCouplingScale(p.type[i], p.type[j]);
                double scalar = -static_cast<double>(factor) * kernel;

                fx += scalar * dxp;
                fy += scalar * dyp;
                fz += scalar * dzp;
                p.vx[j] -= scalar * dxp * strength;
                p.vy[j] -= scalar * dyp * strength;
                p.vz[j] -= scalar * dzp * strength;
            }
        }

        p.vx[i] += fx * strength;
        p.vy[i] += fy * strength;
        p.vz[i] += fz * strength;
    }

    if (regime_index == 3) {
        emitTravelingGluons(p, cells, std::max(RegimeConfig::QGP_GLUON_EMISSION_RADIUS_MIN,
                                               rD * RegimeConfig::QGP_GLUON_EMISSION_RADIUS_RDEBYE_MULT), dt);
        absorbTravelingGluons(p, cells, std::max(RegimeConfig::QGP_GLUON_ABSORPTION_RADIUS_MIN,
                                                 rD * RegimeConfig::QGP_GLUON_ABSORPTION_RADIUS_RDEBYE_MULT));
    }
    applyChargedPlasmaScattering(p, cells, std::max(RegimeConfig::QGP_CHARGED_SCATTER_RADIUS_MIN,
                                                    rD * RegimeConfig::QGP_CHARGED_SCATTER_RADIUS_RDEBYE_MULT), dt);
    decayShortLivedSpecies(p, regime_index, dt);

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE) || !isQgpCarrier(p.type[i])) continue;
        p.x[i] += p.vx[i] * dt;
        p.y[i] += p.vy[i] * dt;
        p.z[i] += p.vz[i] * dt;
        p.vx[i] *= RegimeConfig::QGP_VELOCITY_DAMPING;
        p.vy[i] *= RegimeConfig::QGP_VELOCITY_DAMPING;
        p.vz[i] *= RegimeConfig::QGP_VELOCITY_DAMPING;
    }

    // Decay visual luminosity back toward baseline so interaction flashes fade.
    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        float L = p.luminosity[i];
        if (L > RegimeConfig::QGP_LUMINOSITY_BASELINE) {
            // Exponential-like decay toward 1.0 (visual only)
            p.luminosity[i] = RegimeConfig::QGP_LUMINOSITY_BASELINE
                            + (L - RegimeConfig::QGP_LUMINOSITY_BASELINE) * RegimeConfig::QGP_LUMINOSITY_DECAY_FACTOR;
            if (p.luminosity[i] < RegimeConfig::QGP_LUMINOSITY_DECAY_SNAP_EPSILON) {
                p.luminosity[i] = RegimeConfig::QGP_LUMINOSITY_BASELINE;
            }
        }
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
    const int regime_index = std::clamp(universe.regime_index, 1, 3);
    double a_prev_frame = std::max(prev_scale_factor_, 1e-60);
    double a_new = std::max(scale_factor, 1e-60);
    const double regime_duration = CosmicClock::REGIME_START_TIMES[static_cast<size_t>(regime_index + 1)]
                                 - CosmicClock::REGIME_START_TIMES[static_cast<size_t>(regime_index)];
    double progress_dt = (regime_duration > 0.0) ? cosmic_dt / regime_duration : 0.0;
    const double visual_gain = RegimeConfig::QGP_VISUAL_GAIN_BY_REGIME[static_cast<std::size_t>(regime_index)];
    double total_visual_dt = cosmic_dt <= 0.0 ? 0.0
                                               : std::clamp(progress_dt * visual_gain,
                                                            RegimeConfig::QGP_VISUAL_DT_MIN,
                                                            RegimeConfig::QGP_VISUAL_DT_MAX);
    int substeps = computeSubsteps(total_visual_dt, RegimeConfig::QGP_SUBSTEP_TARGET_DT,
                                   RegimeConfig::QGP_MAX_SUBSTEPS);
    double sub_visual_dt = total_visual_dt / static_cast<double>(substeps);

    for (int step = 0; step < substeps; ++step) {
        double alpha0 = static_cast<double>(step) / static_cast<double>(substeps);
        double alpha1 = static_cast<double>(step + 1) / static_cast<double>(substeps);
        double a_step_prev = interpolatePositive(a_prev_frame, a_new, alpha0);
        double a_step_new = interpolatePositive(a_prev_frame, a_new, alpha1);
        double sub_temp_keV = phys::temperature_keV_from_scale(a_step_new);

        if (universe.particles.x.size() <= RegimeConfig::QGP_MAX_FORCE_PARTICLES && sub_visual_dt > 0.0) {
            applyScreenedCornellForces(universe, sub_temp_keV, sub_visual_dt);
        }

        applyCosmicExpansion(universe, a_step_prev, a_step_new);

        if (regime_index == 3 && sub_temp_keV < CosmicClock::T_QGP_END && !hadronized_) {
            hadronize(universe);
        }
    }

    prev_scale_factor_ = a_new;

    universe.scale_factor    = scale_factor;
    universe.temperature_keV = temp_keV;
}

void RegimeQGP::render(Renderer& renderer, const Universe& universe) {
    renderer.renderParticles(universe);
}
