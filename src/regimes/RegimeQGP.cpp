// src/regimes/RegimeQGP.cpp — Regime 1: Plasma Quark-Glúon
#include "RegimeQGP.hpp"
#include "../core/CosmicClock.hpp"
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
    return type == ParticleType::QUARK_U || type == ParticleType::QUARK_D ||
           type == ParticleType::QUARK_S || type == ParticleType::ANTIQUARK ||
           type == ParticleType::GLUON;
}

bool isQgpQuark(ParticleType type) {
    return type == ParticleType::QUARK_U || type == ParticleType::QUARK_D ||
           type == ParticleType::QUARK_S || type == ParticleType::ANTIQUARK;
}

long long encodeCell(int x, int y, int z) {
    constexpr long long bias = 2048;
    return ((static_cast<long long>(x) + bias) << 42)
         ^ ((static_cast<long long>(y) + bias) << 21)
         ^  (static_cast<long long>(z) + bias);
}

double debyeLength(double temp_keV) {
    double t = std::clamp((temp_keV - CosmicClock::T_QGP_END) / (1.0e16 - CosmicClock::T_QGP_END), 0.0, 1.0);
    return 0.16 - 0.12 * t;
}

void exchangeColorThroughGluons(ParticlePool& p,
                                const std::unordered_map<long long, std::vector<size_t>>& cells,
                                double exchange_radius,
                                double dt)
{
    const double exchange_r2 = exchange_radius * exchange_radius;
    for (size_t gi = 0; gi < p.x.size(); ++gi) {
        if (!(p.flags[gi] & PF_ACTIVE) || p.type[gi] != ParticleType::GLUON) continue;
        if (p.qcd_color[gi] == QcdColor::NONE || p.qcd_anticolor[gi] == QcdColor::NONE) continue;

        int cx = static_cast<int>(std::floor(p.x[gi] / exchange_radius));
        int cy = static_cast<int>(std::floor(p.y[gi] / exchange_radius));
        int cz = static_cast<int>(std::floor(p.z[gi] / exchange_radius));

        size_t emitter = p.x.size();
        size_t receiver = p.x.size();
        double emitter_d2 = exchange_r2;
        double receiver_d2 = exchange_r2;
        QcdColor receiver_color = qcd::receiverColorFromAnticolor(p.qcd_anticolor[gi]);

        for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            auto it = cells.find(encodeCell(cx + dx, cy + dy, cz + dz));
            if (it == cells.end()) continue;

            for (size_t qi : it->second) {
                if (qi == gi || !(p.flags[qi] & PF_ACTIVE) || !isQgpQuark(p.type[qi])) continue;
                double dxq = p.x[qi] - p.x[gi];
                double dyq = p.y[qi] - p.y[gi];
                double dzq = p.z[qi] - p.z[gi];
                double d2 = dxq * dxq + dyq * dyq + dzq * dzq;
                if (d2 > exchange_r2) continue;

                if (p.qcd_color[qi] == p.qcd_color[gi] && d2 < emitter_d2) {
                    emitter = qi;
                    emitter_d2 = d2;
                }
                if (p.qcd_color[qi] == receiver_color && d2 < receiver_d2) {
                    receiver = qi;
                    receiver_d2 = d2;
                }
            }
        }

        if (emitter >= p.x.size() || receiver >= p.x.size() || emitter == receiver) continue;

        p.setQcdCharge(emitter, receiver_color);
        p.setQcdCharge(receiver, p.qcd_color[gi]);

        // Visual cue: briefly boost luminosity of participants so exchanges
        // become visually salient without altering physics.
        p.luminosity[emitter] = std::max(p.luminosity[emitter], 2.2f);
        p.luminosity[receiver] = std::max(p.luminosity[receiver], 2.2f);
        p.luminosity[gi] = std::max(p.luminosity[gi], 3.0f);

        double mx = 0.5 * (p.x[emitter] + p.x[receiver]);
        double my = 0.5 * (p.y[emitter] + p.y[receiver]);
        double mz = 0.5 * (p.z[emitter] + p.z[receiver]);
        p.vx[gi] += (mx - p.x[gi]) * dt * 2.2;
        p.vy[gi] += (my - p.y[gi]) * dt * 2.2;
        p.vz[gi] += (mz - p.z[gi]) * dt * 2.2;
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

    double t = std::clamp((temp_keV - CosmicClock::T_QGP_END) / (1.0e16 - CosmicClock::T_QGP_END), 0.0, 1.0);
    double alpha_s = 0.18 + 0.16 * (1.0 - t);
    double sigma = 0.010 + 0.022 * (1.0 - t);
    double rD = debyeLength(temp_keV);
    double cutoff = std::max(0.12, rD * 3.5);
    double cutoff2 = cutoff * cutoff;
    double softening2 = 1e-4;
    double strength = 0.0014 * dt;

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
        if (!(p.flags[i] & PF_ACTIVE) || !isQgpQuark(p.type[i])) continue;
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
                if (j <= i || !(p.flags[j] & PF_ACTIVE) || !isQgpQuark(p.type[j])) continue;
                double dxp = p.x[j] - p.x[i];
                double dyp = p.y[j] - p.y[i];
                double dzp = p.z[j] - p.z[i];
                double r2 = dxp * dxp + dyp * dyp + dzp * dzp;
                if (r2 < 1e-10 || r2 > cutoff2) continue;

                float factor = qcd::casimirFactor(p.qcd_color[i], p.qcd_anticolor[i],
                                                  p.qcd_color[j], p.qcd_anticolor[j]);
                if (std::abs(factor) < 1e-6f) continue;

                double r = std::sqrt(r2 + softening2);
                double screen = std::exp(-r / std::max(rD, 1e-3));
                double coulomb = alpha_s * screen / (r2 + softening2);
                double confinement = sigma * (1.0 - t) * std::exp(-r / std::max(rD * 1.5, 1e-3));
                double kernel = coulomb + confinement;
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

    exchangeColorThroughGluons(p, cells, std::max(0.08, rD * 1.6), dt);

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE) || !isQgpCarrier(p.type[i])) continue;
        p.x[i] += p.vx[i] * dt;
        p.y[i] += p.vy[i] * dt;
        p.z[i] += p.vz[i] * dt;
        p.vx[i] *= 0.999;
        p.vy[i] *= 0.999;
        p.vz[i] *= 0.999;
    }

    // Decay visual luminosity back toward baseline so interaction flashes fade.
    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        float L = p.luminosity[i];
        if (L > 1.0f) {
            // Exponential-like decay toward 1.0 (visual only)
            p.luminosity[i] = 1.0f + (L - 1.0f) * 0.86f;
            if (p.luminosity[i] < 1.001f) p.luminosity[i] = 1.0f;
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
    double a_prev_frame = std::max(prev_scale_factor_, 1e-60);
    double a_new = std::max(scale_factor, 1e-60);
    constexpr double regime_duration = CosmicClock::REGIME_START_TIMES[2] - CosmicClock::REGIME_START_TIMES[1];
    double progress_dt = (regime_duration > 0.0) ? cosmic_dt / regime_duration : 0.0;
    double total_visual_dt = cosmic_dt <= 0.0 ? 0.0
                                               : std::clamp(progress_dt * 20.0, 0.001, 0.05);
    int substeps = computeSubsteps(total_visual_dt, 0.006, 6);
    double sub_visual_dt = total_visual_dt / static_cast<double>(substeps);

    for (int step = 0; step < substeps; ++step) {
        double alpha0 = static_cast<double>(step) / static_cast<double>(substeps);
        double alpha1 = static_cast<double>(step + 1) / static_cast<double>(substeps);
        double a_step_prev = interpolatePositive(a_prev_frame, a_new, alpha0);
        double a_step_new = interpolatePositive(a_prev_frame, a_new, alpha1);
        double sub_temp_keV = phys::temperature_keV_from_scale(a_step_new);

        if (universe.particles.x.size() <= 10000 && sub_visual_dt > 0.0) {
            applyScreenedCornellForces(universe, sub_temp_keV, sub_visual_dt);
        }

        applyCosmicExpansion(universe, a_step_prev, a_step_new);

        if (sub_temp_keV < 150.0 && !hadronized_) {
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
