// src/regimes/RegimeInflation.cpp — Regime 0: Inflação / Flutuações Quânticas
#include "RegimeInflation.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/SimulationRandom.hpp"
#include "../core/Universe.hpp"
#include "../render/Renderer.hpp"
#include "../physics/Constants.hpp"
#include "../physics/Friedmann.hpp"
#include <array>
#include <chrono>
#include <random>
#include <cmath>
#include <algorithm>

namespace {
std::mt19937 makeFreshInflationRng() {
    std::random_device rd;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::array<std::uint32_t, 8> seed_data = {
        rd(),
        rd(),
        rd(),
        rd(),
        static_cast<std::uint32_t>(now),
        static_cast<std::uint32_t>(now >> 32),
        simrng::globalSeed(),
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&seed_data))
    };
    std::seed_seq seq(seed_data.begin(), seed_data.end());
    return std::mt19937(seq);
}

std::mt19937& inflationRng() {
    static std::mt19937 rng = makeFreshInflationRng();
    return rng;
}

void reseedInflationRng() {
    inflationRng() = makeFreshInflationRng();
}

constexpr double kInflationTotalEFoldsVisual = 55.0;

struct InflationMode {
    float amp;
    float freq_x;
    float freq_y;
    float phase_a;
    float phase_b;
};
}

void RegimeInflation::onEnter(Universe& state) {
    reseedInflationRng();
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
    std::mt19937& rng_inflation = inflationRng();
    int N = PHI_N;
    universe.phi_NX = N;
    universe.phi_NY = N;
    universe.phi_field.assign(static_cast<size_t>(N * N), 0.0f);
    universe.phi_dot_field.assign(static_cast<size_t>(N * N), 0.0f);

    // Semear flutuações quânticas: ruído gaussiano σ = H/2π
    // Usa H₀ como proxy (H real é muito maior durante a inflação, mas a razão importa)
    float sigma = 0.01f;  // amplitude adimensional
    std::normal_distribution<float> noise(0.0f, sigma);
    std::normal_distribution<float> momentum_noise(0.0f, sigma * 0.35f);
    std::uniform_real_distribution<float> phase_dist(0.0f, 2.0f * static_cast<float>(M_PI));
    std::uniform_real_distribution<float> amp_dist(0.12f * sigma, 0.95f * sigma);
    std::uniform_real_distribution<float> freq_dist(0.6f, 5.4f);
    std::uniform_real_distribution<float> offset_dist(-0.22f, 0.22f);

    std::array<InflationMode, 10> modes{};
    for (InflationMode& mode : modes) {
        mode.amp = amp_dist(rng_inflation);
        mode.freq_x = freq_dist(rng_inflation);
        mode.freq_y = freq_dist(rng_inflation);
        mode.phase_a = phase_dist(rng_inflation);
        mode.phase_b = phase_dist(rng_inflation);
    }

    const float cx = 0.5f + offset_dist(rng_inflation);
    const float cy = 0.5f + offset_dist(rng_inflation);
    const float ridge_angle = phase_dist(rng_inflation);
    const float ridge_dir_x = std::cos(ridge_angle);
    const float ridge_dir_y = std::sin(ridge_angle);

    for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) {
        size_t index = static_cast<size_t>(i + N*j);
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(N);
        const float v = (static_cast<float>(j) + 0.5f) / static_cast<float>(N);
        float coherent = 0.0f;
        for (const InflationMode& mode : modes) {
            const float wave_a = std::sin(2.0f * static_cast<float>(M_PI) * (mode.freq_x * u + mode.freq_y * v) + mode.phase_a);
            const float wave_b = std::cos(2.0f * static_cast<float>(M_PI) * (mode.freq_y * u - mode.freq_x * v) + mode.phase_b);
            coherent += mode.amp * wave_a * wave_b;
        }

        const float dx = u - cx;
        const float dy = v - cy;
        const float r2 = dx * dx + dy * dy;
        const float radial_bubble = 1.6f * sigma * std::exp(-r2 * 10.0f)
                                  * std::sin(12.0f * static_cast<float>(std::sqrt(std::max(r2, 1e-5f))) + phase_dist(rng_inflation));
        const float ridge_coord = dx * ridge_dir_x + dy * ridge_dir_y;
        const float ridge = 0.9f * sigma * std::sin(16.0f * ridge_coord + 0.5f * phase_dist(rng_inflation))
                          * std::exp(-(dy * ridge_dir_x - dx * ridge_dir_y) * (dy * ridge_dir_x - dx * ridge_dir_y) * 22.0f);

        universe.phi_field[index] = noise(rng_inflation) + coherent + radial_bubble + ridge;
        universe.phi_dot_field[index] = momentum_noise(rng_inflation) + 0.35f * coherent - 0.18f * ridge;
    }
}

void RegimeInflation::stepScalarField(double cosmic_dt, double H, Universe& universe) {
    std::mt19937& rng_inflation = inflationRng();
    int N = PHI_N;
    constexpr double regime_duration = CosmicClock::REGIME_START_TIMES[1] - CosmicClock::REGIME_START_TIMES[0];
    double progress_dt = (regime_duration > 0.0) ? cosmic_dt / regime_duration : 0.0;
    float dt = cosmic_dt <= 0.0 ? 0.0f
                                : std::clamp(static_cast<float>(progress_dt * 8.0), 0.001f, 0.04f);
    float Hf = std::clamp(static_cast<float>(H), 0.1f, 5.0f); // Cap Hubble visualmente para não matar oscilações
    float dx2 = (1.0f / static_cast<float>(N)) * (1.0f / static_cast<float>(N));

    auto idx = [&](int i, int j) -> int {
        return ((i % N + N) % N) + N * ((j % N + N) % N);
    };

    // Klein-Gordon em 2D: φ̈ + 3Hφ̇ - ∇²φ/a² + V'(φ) = 0
    std::vector<float> phi_new = universe.phi_field;
    std::vector<float> phid_new = universe.phi_dot_field;

    // Gerador rápido de ruído para bombear flutuações quânticas constantes (o campo não "apaga" de vez)
    std::normal_distribution<float> quantum_pump(0.0f, 0.05f * dt);
    const float time_phase = static_cast<float>(e_folds_ * 0.31);

    for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) {
        float phi  = universe.phi_field[idx(i, j)];
        float phid = universe.phi_dot_field[idx(i, j)];
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(N);
        const float v = (static_cast<float>(j) + 0.5f) / static_cast<float>(N);
        // Derivada espacial amortecida visando estética
        float lap  = (universe.phi_field[idx(i+1,j)] + universe.phi_field[idx(i-1,j)]
                    + universe.phi_field[idx(i,j+1)] + universe.phi_field[idx(i,j-1)]
                    - 4.0f * phi) / dx2;
        float lap_diag = (universe.phi_field[idx(i+1,j+1)] + universe.phi_field[idx(i-1,j-1)]
                        + universe.phi_field[idx(i+1,j-1)] + universe.phi_field[idx(i-1,j+1)]
                        - 4.0f * phi) / (2.0f * dx2);
        float coherent_drive = 0.018f * std::sin(2.0f * static_cast<float>(M_PI) * (1.7f * u + 0.9f * v) + time_phase)
                             + 0.013f * std::cos(2.0f * static_cast<float>(M_PI) * (3.1f * u - 1.4f * v) - 1.8f * time_phase)
                             + 0.009f * std::sin(2.0f * static_cast<float>(M_PI) * (4.4f * (u - 0.5f) * (v - 0.5f)) + 0.7f * time_phase + 2.5f * phi);
        float nonlinear_feedback = -0.11f * phi * phi * phi;
        
        // Equação de onda c/ atrito suave da expansão local e poço do potencial M2
        float phi_ddot = -1.5f * Hf * phid + 0.10f * lap + 0.035f * lap_diag - M2 * phi + nonlinear_feedback + coherent_drive;

        phid_new[idx(i,j)] = phid + phi_ddot * dt + quantum_pump(rng_inflation);
        phi_new[idx(i,j)]  = phi  + phid_new[idx(i,j)] * dt;
    }

    // Keep the inflation field visually centered and within a usable dynamic
    // range so it does not collapse into a mostly-blue or mostly-flat frame.
    double mean_phi = 0.0;
    for (float value : phi_new) {
        mean_phi += value;
    }
    mean_phi /= static_cast<double>(phi_new.size());

    double variance = 0.0;
    for (float& value : phi_new) {
        value -= static_cast<float>(mean_phi);
        variance += static_cast<double>(value) * static_cast<double>(value);
    }
    variance /= static_cast<double>(phi_new.size());
    float rms = static_cast<float>(std::sqrt(std::max(variance, 1e-8)));
    float target_rms = 0.02f;
    float renorm = std::clamp(target_rms / rms, 0.85f, 1.2f);
    for (float& value : phi_new) {
        value *= renorm;
    }

    universe.phi_field     = phi_new;
    universe.phi_dot_field = phid_new;

    // Conta e-folds num passo visual normalizado ao intervalo do regime.
    e_folds_ += progress_dt * 55.0;
}

void RegimeInflation::extrudeFieldTo3D(Universe& universe) {
    // Replica o contraste de densidade 2D ao longo de Z com ruído correlacionado
    int N2 = PHI_N;
    int N3 = 64;
    universe.density_field.resize(N3, N3, N3);

    std::normal_distribution<float> noise(0.0f, 0.002f);
    std::mt19937 rng_ext = simrng::makeStream("inflation-extrude");

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
        double remaining_phase_span = std::max(kInflationTotalEFoldsVisual - static_cast<double>(EFOLDS_3D), 1e-6);
        double phase_progress = std::clamp((e_folds_ - static_cast<double>(EFOLDS_3D)) / remaining_phase_span,
                                           0.0,
                                           1.0);
        extrude_t_ = static_cast<float>(phase_progress);
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
