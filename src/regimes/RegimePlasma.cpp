// src/regimes/RegimePlasma.cpp — Regime 3: Plasma de Fótons / Recombinação
#include "RegimePlasma.hpp"
#include "../core/Universe.hpp"
#include "../render/Renderer.hpp"
#include "../physics/FluidGrid.hpp"
#include "../physics/Constants.hpp"
#include "../physics/Friedmann.hpp"
#include <cmath>
#include <algorithm>

static FluidGrid fluid_solver;

void RegimePlasma::onEnter(Universe& state) {
    cmb_flash_triggered_ = false;
    cmb_flash_t_         = 0.0f;
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
    double T_K = phys::keV_to_K(temp_keV);
    double H   = phys::hubble_from_scale(scale_factor);
    baryon_density_ = FluidGrid::baryonDensity(scale_factor);

    fluid_solver.step(universe, cosmic_dt, scale_factor, H, temp_keV);

    // Verifica recombinação via equação de Saha
    double X_e = computeIonizationFraction(T_K, baryon_density_);
    if (X_e < 0.1 && !cmb_flash_triggered_) {
        cmb_flash_triggered_ = true;
        cmb_flash_t_         = 0.0f;
    }

    if (cmb_flash_triggered_ && cmb_flash_t_ < 1.0f) {
        cmb_flash_t_ += static_cast<float>(cosmic_dt / 1e11);  // duração do flash ~1e11 s
        cmb_flash_t_  = std::min(cmb_flash_t_, 1.0f);
    }

    universe.scale_factor    = scale_factor;
    universe.temperature_keV = temp_keV;
}

void RegimePlasma::render(Renderer& renderer, const Universe& universe) {
    renderer.renderVolumeField(universe);
    if (cmb_flash_triggered_) {
        renderer.renderCMBFlash(cmb_flash_t_);
    }
}
