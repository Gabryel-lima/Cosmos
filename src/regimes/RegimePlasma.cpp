// src/regimes/RegimePlasma.cpp — Regime 3: Plasma de Fótons / Recombinação
#include "RegimePlasma.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/Universe.hpp"
#include "../render/Renderer.hpp"
#include "../physics/FluidGrid.hpp"
#include "../physics/Constants.hpp"
#include "../physics/ParticlePool.hpp"
#include "../physics/Friedmann.hpp"
#include <cmath>
#include <algorithm>

static FluidGrid fluid_solver;

void RegimePlasma::onEnter(Universe& state) {
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
        std::srand(42);
        for (int k = 0; k < N; ++k)
        for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            float r = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f)
                      * 0.01f;
            state.density_field.at(i, j, k) = r;
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
    constexpr double regime_duration = CosmicClock::REGIME_START_TIMES[4] - CosmicClock::REGIME_START_TIMES[3];
    double progress_dt = (regime_duration > 0.0) ? cosmic_dt / regime_duration : 0.0;
    double visual_dt = cosmic_dt <= 0.0 ? 0.0
                                         : std::clamp(progress_dt * 24.0, 0.001, 0.04);
    double T_K = phys::keV_to_K(temp_keV);
    double H   = phys::hubble_from_scale(scale_factor);
    baryon_density_ = FluidGrid::baryonDensity(scale_factor);

    if (visual_dt > 0.0) {
        fluid_solver.step(universe, visual_dt, scale_factor, H, temp_keV);
    }

    wave_phase_ += static_cast<float>(visual_dt * 4.0);

    ParticlePool& particles = universe.particles;
    for (size_t i = 0; i < particles.x.size(); ++i) {
        if (!(particles.flags[i] & PF_ACTIVE)) continue;

        double swirl = std::sin(static_cast<double>(wave_phase_) + particles.x[i] * 0.6 + particles.z[i] * 0.3);
        switch (particles.type[i]) {
            case ParticleType::PHOTON:
                particles.vx[i] += swirl * visual_dt * 0.35;
                particles.vy[i] += std::cos(static_cast<double>(wave_phase_) + particles.y[i]) * visual_dt * 0.25;
                particles.luminosity[i] = 1.8f + 0.9f * static_cast<float>(0.5 + 0.5 * swirl);
                break;
            case ParticleType::ELECTRON:
                particles.vx[i] -= swirl * visual_dt * 0.18;
                particles.vz[i] += std::sin(static_cast<double>(wave_phase_) * 0.7 + particles.x[i]) * visual_dt * 0.14;
                break;
            case ParticleType::PROTON:
            case ParticleType::HELIUM4:
            case ParticleType::GAS:
                particles.vy[i] += swirl * visual_dt * 0.08;
                break;
            default:
                break;
        }

        particles.x[i] += particles.vx[i] * visual_dt;
        particles.y[i] += particles.vy[i] * visual_dt;
        particles.z[i] += particles.vz[i] * visual_dt;
        particles.vx[i] *= 0.995;
        particles.vy[i] *= 0.995;
        particles.vz[i] *= 0.995;
    }

    // Verifica recombinação via equação de Saha
    double X_e = computeIonizationFraction(T_K, baryon_density_);
    double neutral_fraction = std::clamp(1.0 - X_e, 0.0, 1.0);
    if (X_e < 0.1 && !cmb_flash_triggered_) {
        cmb_flash_triggered_ = true;
        cmb_flash_t_         = 0.0f;
    }

    if (neutral_fraction > recombined_fraction_) {
        size_t to_convert = static_cast<size_t>((neutral_fraction - recombined_fraction_) * 200.0);
        size_t converted = 0;
        for (size_t i = 0; i < particles.x.size() && converted < to_convert; ++i) {
            if (!(particles.flags[i] & PF_ACTIVE)) continue;
            if (particles.type[i] != ParticleType::PROTON) continue;
            particles.type[i] = ParticleType::GAS;
            ParticlePool::defaultColor(ParticleType::GAS,
                                       particles.color_r[i], particles.color_g[i], particles.color_b[i]);
            particles.luminosity[i] = 1.3f;
            ++converted;
        }

        size_t hidden_electrons = 0;
        for (size_t i = 0; i < particles.x.size() && hidden_electrons < converted; ++i) {
            if (!(particles.flags[i] & PF_ACTIVE)) continue;
            if (particles.type[i] != ParticleType::ELECTRON) continue;
            particles.deactivate(i);
            ++hidden_electrons;
        }
        recombined_fraction_ = neutral_fraction;
    }

    if (cmb_flash_triggered_ && cmb_flash_t_ < 1.0f) {
        cmb_flash_t_ += static_cast<float>(visual_dt * 0.5);
        cmb_flash_t_  = std::min(cmb_flash_t_, 1.0f);
    }

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
