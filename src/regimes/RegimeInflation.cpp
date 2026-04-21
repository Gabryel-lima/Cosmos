// src/regimes/RegimeInflation.cpp — Regime 0: Inflação / Flutuações Quânticas
#include "RegimeInflation.hpp"
#include "../core/Universe.hpp"
#include "../render/Renderer.hpp"
#include "../physics/Constants.hpp"
#include "../physics/Friedmann.hpp"
#include <random>
#include <cmath>
#include <algorithm>

static std::mt19937 rng_inflation(42);

void RegimeInflation::onEnter(Universe& state) {
    e_folds_    = 0.0;
    in_phase_b_ = false;
    extrude_t_  = 0.0f;

    // Inicializa o campo escalar φ(x,y) com semente de flutuação quântica
    initScalarField(state);

    // Alterna para câmera ortográfica na Fase A
    state.inflate_3d_t = 0.0f;
}

void RegimeInflation::onExit() {
    // Nada para limpar
}

void RegimeInflation::initScalarField(Universe& universe) {
    int N = PHI_N;
    universe.phi_NX = N;
    universe.phi_NY = N;
    universe.phi_field.assign(static_cast<size_t>(N * N), 0.0f);
    universe.phi_dot_field.assign(static_cast<size_t>(N * N), 0.0f);

    // Semear flutuações quânticas: ruído gaussiano σ = H/2π
    // Usa H₀ como proxy (H real é muito maior durante a inflação, mas a razão importa)
    float sigma = 0.01f;  // amplitude adimensional
    std::normal_distribution<float> noise(0.0f, sigma);

    for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) {
        universe.phi_field[i + N*j] = noise(rng_inflation);
    }
}

void RegimeInflation::stepScalarField(double cosmic_dt, double H, Universe& universe) {
    int N = PHI_N;
    float dt = static_cast<float>(cosmic_dt);
    float Hf = static_cast<float>(H);
    float dx2 = (1.0f / static_cast<float>(N)) * (1.0f / static_cast<float>(N));

    auto idx = [&](int i, int j) -> int {
        return ((i % N + N) % N) + N * ((j % N + N) % N);
    };

    // Klein-Gordon em 2D: φ̈ + 3Hφ̇ - ∇²φ/a² + V'(φ) = 0
    // V(φ) = m²φ²/2 → V'(φ) = m²φ
    // Discretização: φ̈ = -3Hφ̇ + ∇²φ/a² - M2*φ
    std::vector<float> phi_new = universe.phi_field;
    std::vector<float> phid_new = universe.phi_dot_field;

    for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) {
        float phi  = universe.phi_field[idx(i, j)];
        float phid = universe.phi_dot_field[idx(i, j)];
        float lap  = (universe.phi_field[idx(i+1,j)] + universe.phi_field[idx(i-1,j)]
                    + universe.phi_field[idx(i,j+1)] + universe.phi_field[idx(i,j-1)]
                    - 4.0f * phi) / dx2;
        float phi_ddot = -3.0f * Hf * phid + lap - M2 * phi;

        phid_new[idx(i,j)] = phid + phi_ddot * dt;
        phi_new[idx(i,j)]  = phi  + phid * dt + 0.5f * phi_ddot * dt * dt;
    }
    universe.phi_field     = phi_new;
    universe.phi_dot_field = phid_new;

    // Conta e-folds: N_e += H * dt
    e_folds_ += H * cosmic_dt;
}

void RegimeInflation::extrudeFieldTo3D(Universe& universe) {
    // Replica o contraste de densidade 2D ao longo de Z com ruído correlacionado
    int N2 = PHI_N;
    int N3 = 64;
    universe.density_field.resize(N3, N3, N3);

    std::normal_distribution<float> noise(0.0f, 0.002f);
    std::mt19937 rng_ext(12345);

    for (int k = 0; k < N3; ++k)
    for (int j = 0; j < N3; ++j)
    for (int i = 0; i < N3; ++i) {
        // Mapeia índice 3D para campo 2D (reduz escala)
        int i2 = i * N2 / N3;
        int j2 = j * N2 / N3;
        float phi_val = universe.phi_field[i2 + N2 * j2];
        // Adiciona ruído correlacionado ao longo de Z
        float z_frac = static_cast<float>(k) / static_cast<float>(N3);
        universe.density_field.at(i, j, k) = phi_val + noise(rng_ext) * z_frac;
    }
}

void RegimeInflation::update(double cosmic_dt, double scale_factor, double temp_keV,
                              Universe& universe)
{
    double H = phys::hubble_from_scale(scale_factor);

    stepScalarField(cosmic_dt, H, universe);

    if (!in_phase_b_ && e_folds_ >= EFOLDS_3D) {
        in_phase_b_ = true;
        extrude_t_  = 0.0f;
    }

    if (in_phase_b_) {
        // Conduz extrusão por ~2 unidades de tempo cósmico
        extrude_t_ += static_cast<float>(cosmic_dt * H * 0.05);
        extrude_t_  = std::clamp(extrude_t_, 0.0f, 1.0f);
        universe.inflate_3d_t = extrude_t_;

        if (extrude_t_ > 0.01f) {
            extrudeFieldTo3D(universe);
        }
    }

    // Sincroniza estatísticas do universo
    universe.scale_factor    = scale_factor;
    universe.temperature_keV = temp_keV;
}

void RegimeInflation::render(Renderer& renderer, const Universe& universe) {
    renderer.renderInflationField(universe);
}
