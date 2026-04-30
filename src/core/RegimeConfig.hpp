#pragma once
// src/core/RegimeConfig.hpp — Centralização das variáveis espalhadas ("hardcodeds") que definem
// as contagens, tamanhos de grades e distribuições iniciais de cada regime simulado.

#include <cstddef>
#include <cstdint>
#include <array>

#include "../physics/ParticlePool.hpp"

namespace RegimeConfig {
    constexpr std::uint32_t DEFAULT_RANDOM_SEED = 424242u;

    // Os regimes macro agora dependem mais do volume 3D do que de grandes contagens
    // de partículas. Os presets abaixo privilegiam reduzir custo de CPU (N-body,
    // seeds e microfísica) antes de degradar completamente a resolução volumétrica.
#if defined(QUALITY_SAFE)
    constexpr const char* BUILD_QUALITY_NAME = "SAFE";
    constexpr int    QGP_QUARK_COUNT = 480;
    constexpr int    PLASMA_GRID_SIZE = 24;
    constexpr int    STRUCT_ZELDOVICH_N_CBRT = 9;
    constexpr int    STRUCT_GRID_SIZE = 20;
    constexpr float  STRUCT_BARNES_HUT_THETA = 1.05f;
#elif defined(QUALITY_LOW)
    constexpr const char* BUILD_QUALITY_NAME = "LOW";
    constexpr int    QGP_QUARK_COUNT = 800;
    constexpr int    PLASMA_GRID_SIZE = 32;
    constexpr int    STRUCT_ZELDOVICH_N_CBRT = 12;
    constexpr int    STRUCT_GRID_SIZE = 24;
    constexpr float  STRUCT_BARNES_HUT_THETA = 0.90f;
#elif defined(QUALITY_HIGH)
    constexpr const char* BUILD_QUALITY_NAME = "HIGH";
    constexpr int    QGP_QUARK_COUNT = 2400;
    constexpr int    PLASMA_GRID_SIZE = 64;
    constexpr int    STRUCT_ZELDOVICH_N_CBRT = 24;
    constexpr int    STRUCT_GRID_SIZE = 64;
    constexpr float  STRUCT_BARNES_HUT_THETA = 0.50f;
#elif defined(QUALITY_ULTRA)
    constexpr const char* BUILD_QUALITY_NAME = "ULTRA";
    constexpr int    QGP_QUARK_COUNT = 3600;
    constexpr int    PLASMA_GRID_SIZE = 80;
    constexpr int    STRUCT_ZELDOVICH_N_CBRT = 30;
    constexpr int    STRUCT_GRID_SIZE = 80;
    constexpr float  STRUCT_BARNES_HUT_THETA = 0.42f;
#else
    constexpr const char* BUILD_QUALITY_NAME = "MEDIUM";
    constexpr int    QGP_QUARK_COUNT = 1400;
    constexpr int    PLASMA_GRID_SIZE = 48;
    constexpr int    STRUCT_ZELDOVICH_N_CBRT = 18;
    constexpr int    STRUCT_GRID_SIZE = 40;
    constexpr float  STRUCT_BARNES_HUT_THETA = 0.68f;
#endif

    constexpr int    BASE_QGP_QUARK_COUNT = 1400;
    constexpr int    BASE_PLASMA_GRID_SIZE = 48;
    constexpr int    BASE_STRUCT_GRID_SIZE = 40;
    constexpr double QGP_QUALITY_SCALE = static_cast<double>(QGP_QUARK_COUNT) / static_cast<double>(BASE_QGP_QUARK_COUNT);
    constexpr double PLASMA_QUALITY_SCALE = static_cast<double>(PLASMA_GRID_SIZE) / static_cast<double>(BASE_PLASMA_GRID_SIZE);
    constexpr double STRUCT_QUALITY_SCALE = static_cast<double>(STRUCT_GRID_SIZE) / static_cast<double>(BASE_STRUCT_GRID_SIZE);
    constexpr double QGP_DENSITY_SCALE = 0.5 * (QGP_QUALITY_SCALE + STRUCT_QUALITY_SCALE);
    constexpr double PLASMA_DENSITY_SCALE = 0.5 * (QGP_QUALITY_SCALE + PLASMA_QUALITY_SCALE);

    constexpr int divideOrZero(int value, int divisor) {
        return (divisor > 0) ? (value / divisor) : 0;
    }

    constexpr int minInt(int a, int b) {
        return (a < b) ? a : b;
    }

    constexpr int maxInt(int a, int b) {
        return (a > b) ? a : b;
    }

    constexpr int roundToInt(double value) {
        return static_cast<int>(value >= 0.0 ? value + 0.5 : value - 0.5);
    }

    constexpr int scaledInt(int base, double scale, int min_value = 1) {
        return maxInt(min_value, roundToInt(static_cast<double>(base) * scale));
    }

    constexpr std::size_t scaledSize(std::size_t base, double scale, std::size_t min_value = 1) {
        const auto scaled = static_cast<std::size_t>(roundToInt(static_cast<double>(base) * scale));
        return (scaled > min_value) ? scaled : min_value;
    }

    constexpr double scaledDouble(double base, double scale) {
        return base * scale;
    }

    struct RelativisticSpeciesRecipe {
        ParticleType type;
        double weight;
        float luminosity;
    };

    struct RelativisticRecipeView {
        const RelativisticSpeciesRecipe* data;
        std::size_t size;
    };

    struct EarlyRelativisticSeedConfig {
        double radius;
        double min_separation_scale;
        double velocity_sigma;
        double target_count_multiplier;
    };

    // ── Regime 1: Quark-Gluon Plasma (QGP) ──
    constexpr int    QGP_GLUON_RATIO_DIVISOR = 5;      // 1 glúon para cada N quarks (ex: N / 5)
    constexpr double QGP_INIT_MIN_SEPARATION = scaledDouble(0.020, 1.0 / QGP_DENSITY_SCALE);
    constexpr int    QGP_GLUON_COUNT = divideOrZero(QGP_QUARK_COUNT, QGP_GLUON_RATIO_DIVISOR);
    constexpr int    QGP_MAX_HADRONIZABLE_BARYONS = minInt(divideOrZero(QGP_QUARK_COUNT, 3), QGP_GLUON_COUNT);

    constexpr std::array<RelativisticSpeciesRecipe, 30> REHEATING_PARTICLE_RECIPE = {{
        {ParticleType::QUARK_U, 0.060, 2.2f}, {ParticleType::QUARK_D, 0.060, 2.2f},
        {ParticleType::QUARK_S, 0.050, 2.3f}, {ParticleType::QUARK_C, 0.045, 2.4f},
        {ParticleType::QUARK_B, 0.030, 2.5f}, {ParticleType::QUARK_T, 0.015, 2.6f},
        {ParticleType::ANTIQUARK_U, 0.060, 2.2f}, {ParticleType::ANTIQUARK_D, 0.060, 2.2f},
        {ParticleType::ANTIQUARK_S, 0.050, 2.3f}, {ParticleType::ANTIQUARK_C, 0.045, 2.4f},
        {ParticleType::ANTIQUARK_B, 0.030, 2.5f}, {ParticleType::ANTIQUARK_T, 0.015, 2.6f},
        {ParticleType::GLUON, 0.200, 2.9f}, {ParticleType::PHOTON, 0.045, 2.2f},
        {ParticleType::ELECTRON, 0.022, 1.3f}, {ParticleType::POSITRON, 0.022, 1.3f},
        {ParticleType::MUON, 0.020, 1.35f}, {ParticleType::ANTIMUON, 0.020, 1.35f},
        {ParticleType::TAU, 0.012, 1.4f}, {ParticleType::ANTITAU, 0.012, 1.4f},
        {ParticleType::NEUTRINO_E, 0.018, 0.55f}, {ParticleType::ANTINEUTRINO_E, 0.018, 0.55f},
        {ParticleType::NEUTRINO_MU, 0.018, 0.55f}, {ParticleType::ANTINEUTRINO_MU, 0.018, 0.55f},
        {ParticleType::NEUTRINO_TAU, 0.018, 0.55f}, {ParticleType::ANTINEUTRINO_TAU, 0.018, 0.55f},
        {ParticleType::W_BOSON_POS, 0.014, 1.8f}, {ParticleType::W_BOSON_NEG, 0.014, 1.8f},
        {ParticleType::Z_BOSON, 0.010, 1.7f}, {ParticleType::HIGGS_BOSON, 0.006, 1.75f},
    }};

    constexpr std::array<RelativisticSpeciesRecipe, 27> LEPTON_ERA_PARTICLE_RECIPE = {{
        {ParticleType::QUARK_U, 0.070, 2.2f}, {ParticleType::QUARK_D, 0.070, 2.2f},
        {ParticleType::QUARK_S, 0.060, 2.3f}, {ParticleType::QUARK_C, 0.020, 2.35f},
        {ParticleType::ANTIQUARK_U, 0.070, 2.2f}, {ParticleType::ANTIQUARK_D, 0.070, 2.2f},
        {ParticleType::ANTIQUARK_S, 0.060, 2.3f}, {ParticleType::ANTIQUARK_C, 0.020, 2.35f},
        {ParticleType::GLUON, 0.160, 2.8f}, {ParticleType::PHOTON, 0.070, 2.1f},
        {ParticleType::ELECTRON, 0.045, 1.25f}, {ParticleType::POSITRON, 0.045, 1.25f},
        {ParticleType::MUON, 0.035, 1.3f}, {ParticleType::ANTIMUON, 0.035, 1.3f},
        {ParticleType::TAU, 0.010, 1.35f}, {ParticleType::ANTITAU, 0.010, 1.35f},
        {ParticleType::NEUTRINO_E, 0.030, 0.5f}, {ParticleType::ANTINEUTRINO_E, 0.030, 0.5f},
        {ParticleType::NEUTRINO_MU, 0.030, 0.5f}, {ParticleType::ANTINEUTRINO_MU, 0.030, 0.5f},
        {ParticleType::NEUTRINO_TAU, 0.022, 0.5f}, {ParticleType::ANTINEUTRINO_TAU, 0.022, 0.5f},
        {ParticleType::W_BOSON_POS, 0.008, 1.55f}, {ParticleType::W_BOSON_NEG, 0.008, 1.55f},
        {ParticleType::Z_BOSON, 0.007, 1.5f},
    }};

    constexpr std::array<RelativisticSpeciesRecipe, 14> QGP_ERA_PARTICLE_RECIPE = {{
        {ParticleType::QUARK_U, 0.120, 2.2f}, {ParticleType::QUARK_D, 0.120, 2.2f},
        {ParticleType::QUARK_S, 0.090, 2.3f}, {ParticleType::ANTIQUARK_U, 0.110, 2.2f},
        {ParticleType::ANTIQUARK_D, 0.110, 2.2f}, {ParticleType::ANTIQUARK_S, 0.085, 2.3f},
        {ParticleType::GLUON, 0.220, 2.85f}, {ParticleType::PHOTON, 0.040, 1.9f},
        {ParticleType::ELECTRON, 0.028, 1.15f}, {ParticleType::POSITRON, 0.028, 1.15f},
        {ParticleType::NEUTRINO_E, 0.016, 0.45f}, {ParticleType::ANTINEUTRINO_E, 0.016, 0.45f},
        {ParticleType::NEUTRINO_MU, 0.016, 0.45f}, {ParticleType::ANTINEUTRINO_MU, 0.016, 0.45f},
    }};

    constexpr std::array<EarlyRelativisticSeedConfig, 4> EARLY_RELATIVISTIC_SEED_CONFIGS = {{
        {0.0, 1.0, 0.0, 1.0},
        {0.62, 0.85, 0.08, 1.90},
        {0.56, 0.90, 0.06, 1.45},
        {0.50, 1.00, 0.05, 1.20},
    }};

    constexpr RelativisticRecipeView relativisticRecipeForRegime(int regime_index) {
        switch (regime_index) {
            case 1: return {REHEATING_PARTICLE_RECIPE.data(), REHEATING_PARTICLE_RECIPE.size()};
            case 2: return {LEPTON_ERA_PARTICLE_RECIPE.data(), LEPTON_ERA_PARTICLE_RECIPE.size()};
            case 3: return {QGP_ERA_PARTICLE_RECIPE.data(), QGP_ERA_PARTICLE_RECIPE.size()};
            default: return {nullptr, 0};
        }
    }

    constexpr EarlyRelativisticSeedConfig relativisticSeedConfigForRegime(int regime_index) {
        return (regime_index >= 1 && regime_index <= 3)
            ? EARLY_RELATIVISTIC_SEED_CONFIGS[static_cast<std::size_t>(regime_index)]
            : EARLY_RELATIVISTIC_SEED_CONFIGS[0];
    }

    constexpr int relativisticTargetCount(int regime_index) {
        const EarlyRelativisticSeedConfig cfg = relativisticSeedConfigForRegime(regime_index);
        return static_cast<int>(QGP_QUARK_COUNT * cfg.target_count_multiplier);
    }

    constexpr double QGP_PAIR_SCALE_GLUON_GLUON = 1.35;
    constexpr double QGP_PAIR_SCALE_MIXED_GLUON = 1.12;
    constexpr double QGP_DEBYE_LENGTH_COLD = 0.16;
    constexpr double QGP_DEBYE_LENGTH_HOT_DELTA = 0.12;
    constexpr double QGP_GLUON_SPAWN_OFFSET = 0.008;
    constexpr double QGP_GLUON_TRAVEL_SPEED_BASE = 0.22;
    constexpr double QGP_GLUON_TRAVEL_SPEED_BOOST = 0.08;
    constexpr double QGP_GLUON_TRAVEL_DT_SCALE = scaledDouble(120.0, QGP_QUALITY_SCALE);
    constexpr float  QGP_LUMINOSITY_EMITTER_MIN = 2.5f;
    constexpr float  QGP_LUMINOSITY_RECEIVER_MIN = 2.1f;
    constexpr float  QGP_LUMINOSITY_GLUON_MIN = 3.2f;
    constexpr float  QGP_LUMINOSITY_ABSORPTION_RECEIVER_MIN = 2.6f;
    constexpr double QGP_ABSORBED_GLUON_VELOCITY_RETAIN = 0.25;
    constexpr float  QGP_ABSORBED_GLUON_LUMINOSITY = 1.2f;
    constexpr double QGP_CHARGED_SCATTER_STRENGTH = scaledDouble(0.00045, QGP_DENSITY_SCALE);
    constexpr double QGP_HEAVY_DECAY_RATE_REHEAT = 1.8;
    constexpr double QGP_HEAVY_DECAY_RATE_LEPTON = 0.9;
    constexpr double QGP_HEAVY_DECAY_MAX_PROBABILITY = 0.35;
    constexpr float  QGP_HEAVY_DECAY_LUMINOSITY_FLOOR = 0.9f;
    constexpr float  QGP_HEAVY_DECAY_LUMINOSITY_RETAIN = 0.8f;
    constexpr double QGP_ALPHA_S_BASE = 0.18;
    constexpr double QGP_ALPHA_S_COLD_DELTA = 0.16;
    constexpr double QGP_STRING_TENSION_BASE = 0.010;
    constexpr double QGP_STRING_TENSION_COLD_DELTA = 0.022;
    constexpr double QGP_FORCE_CUTOFF_MIN = 0.12;
    constexpr double QGP_FORCE_CUTOFF_RDEBYE_MULT = 3.5;
    constexpr double QGP_FORCE_STRENGTH = 0.0014;
    constexpr double QGP_GLUON_EMISSION_RADIUS_MIN = scaledDouble(0.10, 1.0 / QGP_DENSITY_SCALE);
    constexpr double QGP_GLUON_EMISSION_RADIUS_RDEBYE_MULT = 1.8;
    constexpr double QGP_GLUON_ABSORPTION_RADIUS_MIN = scaledDouble(0.05, 1.0 / QGP_DENSITY_SCALE);
    constexpr double QGP_GLUON_ABSORPTION_RADIUS_RDEBYE_MULT = 1.1;
    constexpr double QGP_CHARGED_SCATTER_RADIUS_MIN = scaledDouble(0.10, 1.0 / QGP_DENSITY_SCALE);
    constexpr double QGP_CHARGED_SCATTER_RADIUS_RDEBYE_MULT = 2.0;
    constexpr double QGP_VELOCITY_DAMPING = 0.999;
    constexpr float  QGP_LUMINOSITY_BASELINE = 1.0f;
    constexpr float  QGP_LUMINOSITY_DECAY_FACTOR = 0.86f;
    constexpr float  QGP_LUMINOSITY_DECAY_SNAP_EPSILON = 1.001f;
    constexpr std::array<double, 4> QGP_VISUAL_GAIN_BY_REGIME = {{0.0,
                                                                  scaledDouble(30.0, QGP_QUALITY_SCALE),
                                                                  scaledDouble(24.0, QGP_QUALITY_SCALE),
                                                                  scaledDouble(20.0, QGP_QUALITY_SCALE)}};
    constexpr double QGP_VISUAL_DT_MIN = 0.001;
    constexpr double QGP_VISUAL_DT_MAX = 0.05;
    constexpr double QGP_SUBSTEP_TARGET_DT = 0.006;
    constexpr int    QGP_MAX_SUBSTEPS = scaledInt(6, 0.75 + 0.25 * QGP_QUALITY_SCALE);
    constexpr std::size_t QGP_MAX_FORCE_PARTICLES = scaledSize(10000, QGP_QUALITY_SCALE, 4000);

    constexpr double plasmaFusionBaseProbability(ParticleType product) {
        switch (product) {
            case ParticleType::DEUTERIUM: return 0.08;
            case ParticleType::HELIUM3: return 0.045;
            case ParticleType::HELIUM4NUCLEI: return 0.025;
            case ParticleType::LITHIUM7: return 0.010;
            default: return 0.0;
        }
    }

    constexpr double PLASMA_INTERACTION_CELL_SIZE_MULT = scaledDouble(1.4, 1.0 / PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_NEUTRON_DECAY_RATE = 0.05;
    constexpr double PLASMA_NEUTRON_DECAY_NEUTRAL_FACTOR = 0.5;
    constexpr double PLASMA_NEUTRON_DECAY_MAX_PROBABILITY = 0.08;
    constexpr double PLASMA_NEUTRON_DECAY_TEMP_BOOST = 0.35;
    constexpr float  PLASMA_NEUTRON_DECAY_PHOTON_LUMINOSITY = 1.6f;
    constexpr double PLASMA_NEUTRON_DECAY_PHOTON_SPEED = 0.20;
    constexpr double PLASMA_ELECTROSTATIC_FORCE = scaledDouble(0.025, PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_ELECTROSTATIC_SOFTENING = 0.02;
    constexpr double PLASMA_NEUTRAL_ATTRACTION_FORCE = scaledDouble(0.004, PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_PHOTON_ELECTRON_SCATTER_FORCE = scaledDouble(0.08, PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_PHOTON_ELECTRON_RECOIL_MULT = 1.6;
    constexpr double PLASMA_PHOTON_ELECTRON_TEMP_BOOST = 0.05;
    constexpr float  PLASMA_PHOTON_SCATTER_LUMINOSITY_FLOOR = 0.4f;
    constexpr float  PLASMA_PHOTON_SCATTER_LUMINOSITY_RETAIN = 0.98f;
    constexpr double PLASMA_IONIZATION_BASE_RATE = scaledDouble(0.10, PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_IONIZATION_TEMPERATURE_SCALE = 1.2;
    constexpr double PLASMA_IONIZATION_NEUTRAL_FACTOR = 0.35;
    constexpr double PLASMA_IONIZATION_MAX_PROBABILITY = 0.22;
    constexpr double PLASMA_IONIZATION_TEMP_BOOST = 0.3;
    constexpr double PLASMA_CAPTURE_BASE_RATE = scaledDouble(0.08, PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_CAPTURE_NEUTRAL_SCALE = 0.18;
    constexpr double PLASMA_CAPTURE_TEMP_BASE = 0.6;
    constexpr double PLASMA_CAPTURE_TEMP_SCALE = 0.15;
    constexpr double PLASMA_CAPTURE_MAX_PROBABILITY = 0.26;
    constexpr double PLASMA_CAPTURE_TEMP_BOOST = 0.22;
    constexpr float  PLASMA_CAPTURE_PHOTON_LUMINOSITY = 1.8f;
    constexpr double PLASMA_CAPTURE_PHOTON_SPEED = 0.14;
    constexpr double PLASMA_NEUTRAL_EMISSION_TEMP_THRESHOLD = 0.3;
    constexpr double PLASMA_NEUTRAL_EMISSION_RATE = scaledDouble(0.12, PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_NEUTRAL_EMISSION_MAX_PROBABILITY = 0.18;
    constexpr double PLASMA_NEUTRAL_EMISSION_TEMP_RETAIN = 0.6;
    constexpr float  PLASMA_NEUTRAL_EMISSION_PHOTON_LUMINOSITY = 1.4f;
    constexpr double PLASMA_NEUTRAL_EMISSION_PHOTON_SPEED = 0.12;
    constexpr float  PLASMA_FUSION_LUMINOSITY_MAX = 2.4f;
    constexpr float  PLASMA_FUSION_LUMINOSITY_BOOST = 0.6f;
    constexpr float  PLASMA_FUSION_TEMP_BOOST = 0.6f;
    constexpr float  PLASMA_FUSION_PHOTON_LUMINOSITY = 2.0f;
    constexpr double PLASMA_FUSION_PHOTON_SPEED = 0.16;
    constexpr std::size_t PLASMA_MICROPHYSICS_MAX_PARTICLES = scaledSize(9000, PLASMA_QUALITY_SCALE, 3000);
    constexpr double PLASMA_ELECTRON_EMISSION_SPEED = 0.18;
    constexpr float  PLASMA_ELECTRON_EMISSION_LUMINOSITY = 0.85f;
    constexpr double PLASMA_NEUTRINO_EMISSION_SPEED = 0.28;
    constexpr float  PLASMA_NEUTRINO_EMISSION_LUMINOSITY = 0.22f;
    constexpr double PLASMA_VISUAL_GAIN = scaledDouble(24.0, PLASMA_QUALITY_SCALE);
    constexpr double PLASMA_VISUAL_DT_MIN = 0.001;
    constexpr double PLASMA_VISUAL_DT_MAX = 0.04;
    constexpr double PLASMA_SUBSTEP_TARGET_DT = 0.006;
    constexpr int    PLASMA_MAX_SUBSTEPS = scaledInt(8, 0.75 + 0.25 * PLASMA_QUALITY_SCALE);
    constexpr double PLASMA_WAVE_PHASE_SPEED = 4.0;
    constexpr double PLASMA_WAVE_RADIUS_PHASE_SCALE = 0.9;
    constexpr double PLASMA_WAVE_INDEX_PHASE_SCALE = 0.017;
    constexpr double PLASMA_WAVE_TANGENT_PHASE_A = 0.73;
    constexpr double PLASMA_WAVE_TANGENT_PHASE_B = 1.11;
    constexpr double PLASMA_WAVE_TANGENT_PHASE_C = 1.41;
    constexpr double PLASMA_WAVE_TANGENT_OFFSET_A = 0.2;
    constexpr double PLASMA_WAVE_TANGENT_OFFSET_B = 1.3;
    constexpr double PLASMA_WAVE_TANGENT_OFFSET_C = 2.1;
    constexpr double PLASMA_WAVE_FALLBACK_AXIS = 0.577;
    constexpr double PLASMA_WAVE_REORTHOGONALIZE_DOT = 0.92;
    constexpr double PLASMA_WAVE_PULSE_FREQUENCY = 0.63;
    constexpr double PLASMA_PHOTON_SWIRL_FORCE = scaledDouble(0.35, PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_PHOTON_PULSE_FORCE = scaledDouble(0.18, PLASMA_DENSITY_SCALE);
    constexpr float  PLASMA_PHOTON_LUMINOSITY_BASE = 1.8f;
    constexpr float  PLASMA_PHOTON_LUMINOSITY_SWIRL = 0.9f;
    constexpr double PLASMA_ELECTRON_SWIRL_FORCE = scaledDouble(0.18, PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_ELECTRON_PULSE_FORCE = scaledDouble(0.12, PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_BARYON_SWIRL_FORCE = scaledDouble(0.05, PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_PARTICLE_VELOCITY_DAMPING = 0.998;
    constexpr double PLASMA_RECOMBINATION_TRIGGER_XE = 0.1;
    constexpr int    PLASMA_RECOMBINATION_PHOTON_BUDGET_DIVISOR = 2;
    constexpr double PLASMA_RECOMBINATION_TEMP_BOOST = 0.18;
    constexpr float  PLASMA_RECOMBINATION_PHOTON_LUMINOSITY = 1.5f;
    constexpr double PLASMA_RECOMBINATION_PHOTON_SPEED = 0.10;
    constexpr double PLASMA_CMB_FLASH_RATE = 0.5;
    constexpr float  PLASMA_BAO_NOISE_AMPLITUDE = 0.005f;

    // ── Regime 2: Big Bang Nucleosynthesis (BBN) ──
    constexpr int    BBN_NUCLEON_COUNT = QGP_MAX_HADRONIZABLE_BARYONS;
    constexpr int    BBN_PROTON_RATIO = 8;             // 1 nêutron para cada 7 prótons (i % 8 == 0 -> de um tipo)
    constexpr double BBN_INIT_XP = 0.875;
    constexpr double BBN_INIT_XN = 0.125;
    constexpr double BBN_INIT_MIN_SEPARATION = 0.016;

    // ── Regime 3: Photon Plasma / Recombination ──
    constexpr int    PLASMA_BARYON_COUNT = BBN_NUCLEON_COUNT;
    constexpr int    PLASMA_PHOTON_RATIO_NUMERATOR = 16;   // legado: 3200 / 1800 ≈ 16 / 9
    constexpr int    PLASMA_PHOTON_RATIO_DENOMINATOR = 9;
    constexpr int    PLASMA_PHOTON_COUNT = divideOrZero(
        PLASMA_BARYON_COUNT * PLASMA_PHOTON_RATIO_NUMERATOR + (PLASMA_PHOTON_RATIO_DENOMINATOR / 2),
        PLASMA_PHOTON_RATIO_DENOMINATOR);
    constexpr int    PLASMA_HELIUM_RATIO_DIVISOR = 7;  // Partículas alfa: 1 a cada 7 bárions
    constexpr double PLASMA_INIT_BARYON_MIN_SEPARATION = scaledDouble(0.070, 1.0 / PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_INTERACTION_RADIUS = scaledDouble(0.18, 1.0 / PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_CAPTURE_RADIUS = scaledDouble(0.08, 1.0 / PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_FUSION_RADIUS = scaledDouble(0.05, 1.0 / PLASMA_DENSITY_SCALE);
    constexpr double PLASMA_PHOTON_SCATTER_RADIUS = scaledDouble(0.12, 1.0 / PLASMA_DENSITY_SCALE);
    constexpr int    PLASMA_MAX_MICRO_PHOTONS = scaledInt(18, PLASMA_QUALITY_SCALE, 6);
    constexpr int    PLASMA_MAX_MICRO_FUSIONS = scaledInt(6, PLASMA_QUALITY_SCALE, 2);
    constexpr int    PLASMA_MAX_MICRO_DECAYS = scaledInt(3, PLASMA_QUALITY_SCALE, 1);

    // ── Regime 4: Structure Formation ──
    constexpr double STRUCT_BOX_SIZE_MPC = 50.0;       // Tamanho cúbico da simulação inicial
    constexpr int    STRUCT_GAS_RATIO_DIVISOR = 5;     // 20% gás (i % 5 == 0), 80% DM

    // ── Massas e Constantes de Astro-formação Inicial ──
    constexpr double MASS_GAS = 2.0e6;
    constexpr double MASS_DARK_MATTER = 8.0e6;
    constexpr double MASS_STAR = 5.0e7;
    constexpr double MASS_BLACKHOLE = 2.0e8;

    // Frequência de geração (1 em cada N partículas se torna X_TYPE)
    constexpr size_t STRUCT_STAR_SPAWN_STEP = 320;
    constexpr size_t STRUCT_BH_SPAWN_STEP = 2400;
    constexpr size_t STRUCT_BH_SPAWN_OFFSET = 150;

    // Transição de Plasma -> Formação de Estruturas: chance de formar protostar a partir de gás
    constexpr size_t TRANS_STRUCT_STAR_SPAWN_STEP = 96;

    // --- Struct-like groups matching `Universe.hpp` types ---
    // These provide names that mirror the structs in Universe for easier mapping.
    struct NuclearAbundances {
        static constexpr double Xp = BBN_INIT_XP;
        static constexpr double Xn = BBN_INIT_XN;
        static constexpr int    NucleonCount = BBN_NUCLEON_COUNT;
        static constexpr int    ProtonRatio = BBN_PROTON_RATIO;
    };

    struct QualityProfile {
        static constexpr int   N_particles = STRUCT_ZELDOVICH_N_CBRT * STRUCT_ZELDOVICH_N_CBRT * STRUCT_ZELDOVICH_N_CBRT;
        static constexpr int   grid_res    = STRUCT_GRID_SIZE;
        static constexpr float barnes_hut_theta = STRUCT_BARNES_HUT_THETA;
    };

    struct CameraState {
        static constexpr double pos_x = 0.0;
        static constexpr double pos_y = 0.0;
        static constexpr double pos_z = 5.0;
        static constexpr float  fwd_x = 0.0f;
        static constexpr float  fwd_y = 0.0f;
        static constexpr float  fwd_z = -1.0f;
        static constexpr double zoom  = 100.0;
        static constexpr bool   ortho = false;
    };

    struct GridDataDefaults {
        static constexpr int default_NX = PLASMA_GRID_SIZE;
        static constexpr int default_NY = PLASMA_GRID_SIZE;
        static constexpr int default_NZ = PLASMA_GRID_SIZE;
    };
}
