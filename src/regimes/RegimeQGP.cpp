// src/regimes/RegimeQGP.cpp — Regime 1: Plasma Quark-Glúon
#include "RegimeQGP.hpp"
#include "../core/Universe.hpp"
#include "../render/Renderer.hpp"
#include "../physics/Constants.hpp"
#include "../physics/ParticlePool.hpp"
#include <random>
#include <cmath>
#include <algorithm>

static std::mt19937 rng_qgp(7);

void RegimeQGP::onEnter(Universe& state) {
    prev_scale_factor_ = state.scale_factor;
    hadronized_        = false;
    // Partículas já posicionadas por RegimeManager::buildInitialState()
}

void RegimeQGP::onExit() {}

// ── Interações Yukawa de curto alcance ────────────────────────────────────────────
// V(r) = g² exp(-r/λ) / r, λ = comprimento de Debye ∝ 1/T

void RegimeQGP::applyYukawaForces(Universe& universe, double temp_keV, double dt) {
    ParticlePool& p = universe.particles;
    size_t n = p.x.size();
    if (n == 0) return;

    // Comprimento de blindagem de Debye em unidades de simulação
    // λ = 0.1 fm em T=150 MeV; escala como λ ∝ 1/T
    double lambda = 1e-15 / (temp_keV / 150.0 + 1.0);  // metros
    // Em unidades de sim (normalizado para caixa ≈ 1), escala adequadamente
    double sim_scale = 1e-13;  // unidade de sim = 0.1 pm
    double lam_sim = lambda / sim_scale;
    double g2 = 1.0;  // intensidade de acoplamento (adimensional para visualização)

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        double fx = 0, fy = 0, fz = 0;
        for (size_t j = 0; j < n; ++j) {
            if (i == j || !(p.flags[j] & PF_ACTIVE)) continue;
            double dx = p.x[j] - p.x[i];
            double dy = p.y[j] - p.y[i];
            double dz = p.z[j] - p.z[i];
            double r2 = dx*dx + dy*dy + dz*dz;
            if (r2 < 1e-30) continue;
            double r = std::sqrt(r2);
            // Força: dV/dr = -g² exp(-r/λ) * (1/r + 1/λ) / r
            // sinal: repulsivo para cargas de cor iguais, simplificado como repulsão aqui
            double mag = g2 * std::exp(-r / lam_sim) * (1.0/r + 1.0/lam_sim) / r;
            // Usa sinal de carga para atração/repulsão
            double sign = static_cast<double>(p.charge[i] * p.charge[j]);
            if (sign == 0) sign = -1.0;  // neutro em cor: atrai quarks próximos
            double force = sign * mag;
            fx += force * dx / r;
            fy += force * dy / r;
            fz += force * dz / r;
        }
        double inv_m = (p.mass[i] > 0) ? 1.0 / p.mass[i] : 0.0;
        p.vx[i] += fx * inv_m * dt;
        p.vy[i] += fy * inv_m * dt;
        p.vz[i] += fz * inv_m * dt;
        p.x[i]  += p.vx[i] * dt;
        p.y[i]  += p.vy[i] * dt;
        p.z[i]  += p.vz[i] * dt;
    }
}

void RegimeQGP::applyCosmicExpansion(Universe& universe, double a_prev, double a_new) {
    if (a_prev <= 0.0) return;
    double ratio = a_new / a_prev;
    ParticlePool& p = universe.particles;
    for (size_t i = 0; i < p.x.size(); ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        p.x[i] *= ratio;
        p.y[i] *= ratio;
        p.z[i] *= ratio;
        // Velocidades peculiares diminuem: v ∝ 1/a
        p.vx[i] /= ratio;
        p.vy[i] /= ratio;
        p.vz[i] /= ratio;
    }
}

// ── Hadronização em T < 150 keV ─────────────────────────────────────────────
void RegimeQGP::hadronize(Universe& universe) {
    if (hadronized_) return;
    hadronized_ = true;

    ParticlePool& p = universe.particles;
    size_t n = p.x.size();
    double confinement_radius_sim = 1e-15 / 1e-13;  // 1 fm em unidades de sim

    // Agrupamento de tripletos guloso simples: encontra tripletos de quarks dentro do raio
    std::vector<bool> assigned(n, false);
    std::vector<size_t> quarks;
    for (size_t i = 0; i < n; ++i) {
        if ((p.flags[i] & PF_ACTIVE) &&
            (p.type[i] == ParticleType::QUARK_U ||
             p.type[i] == ParticleType::QUARK_D ||
             p.type[i] == ParticleType::QUARK_S)) {
            quarks.push_back(i);
        }
    }

    for (size_t qi = 0; qi < quarks.size(); ++qi) {
        size_t i = quarks[qi];
        if (assigned[i]) continue;
        // Find 2 nearest unassigned quarks
        std::vector<std::pair<double,size_t>> nearby;
        for (size_t qj = qi+1; qj < quarks.size(); ++qj) {
            size_t j = quarks[qj];
            if (assigned[j]) continue;
            double dx = p.x[i]-p.x[j], dy = p.y[i]-p.y[j], dz = p.z[i]-p.z[j];
            double r = std::sqrt(dx*dx+dy*dy+dz*dz);
            if (r < confinement_radius_sim * 3.0)
                nearby.push_back({r, j});
        }
        if (nearby.size() >= 2) {
            std::sort(nearby.begin(), nearby.end());
            size_t j = nearby[0].second;
            size_t k = nearby[1].second;
            // Funde em próton ou nêtron no centro de massa
            double cx = (p.x[i]+p.x[j]+p.x[k])/3.0;
            double cy = (p.y[i]+p.y[j]+p.y[k])/3.0;
            double cz = (p.z[i]+p.z[j]+p.z[k])/3.0;
            double cvx = (p.vx[i]+p.vx[j]+p.vx[k])/3.0;
            double cvy = (p.vy[i]+p.vy[j]+p.vy[k])/3.0;
            double cvz = (p.vz[i]+p.vz[j]+p.vz[k])/3.0;
            // Determina próton ou nêtron com base no conteúdo de quarks
            int u_count = 0;
            for (size_t idx : {i, j, k}) {
                if (p.type[idx] == ParticleType::QUARK_U) ++u_count;
            }
            ParticleType hadron = (u_count >= 2) ? ParticleType::PROTON : ParticleType::NEUTRON;
            float cr, cg, cb;
            ParticlePool::defaultColor(hadron, cr, cg, cb);

            // Desativa quarks
            p.deactivate(i); p.deactivate(j); p.deactivate(k);
            assigned[i] = assigned[j] = assigned[k] = true;

            p.add(cx, cy, cz, cvx, cvy, cvz, phys::m_p, hadron, cr, cg, cb);
        }
    }
}

void RegimeQGP::update(double cosmic_dt, double scale_factor, double temp_keV,
                        Universe& universe)
{
    double a_new = scale_factor;

    // Aplica forças Yukawa (apenas para contagens pequenas de partículas — O(N²))
    // Para N > 5000, pula integração de força, apenas expande
    if (universe.particles.x.size() <= 5000) {
        applyYukawaForces(universe, temp_keV, cosmic_dt);
    }

    applyCosmicExpansion(universe, prev_scale_factor_, a_new);
    prev_scale_factor_ = a_new;

    if (temp_keV < 150.0 && !hadronized_) {
        hadronize(universe);
    }

    universe.scale_factor    = scale_factor;
    universe.temperature_keV = temp_keV;
}

void RegimeQGP::render(Renderer& renderer, const Universe& universe) {
    renderer.renderParticles(universe);
}
