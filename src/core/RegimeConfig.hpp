#pragma once
// src/core/RegimeConfig.hpp — Centralização das variáveis espalhadas ("hardcodeds") que definem
// as contagens, tamanhos de grades e distribuições iniciais de cada regime simulado.

#include <cstddef>

namespace RegimeConfig {
    // ── Regime 1: Quark-Gluon Plasma (QGP) ──
    constexpr int    QGP_QUARK_COUNT = 2000;
    constexpr int    QGP_GLUON_RATIO_DIVISOR = 5;      // 1 glúon para cada N quarks (ex: N / 5)

    // ── Regime 2: Big Bang Nucleosynthesis (BBN) ──
    constexpr int    BBN_NUCLEON_COUNT = 10000;
    constexpr int    BBN_PROTON_RATIO = 8;             // 1 nêutron para cada 7 prótons (i % 8 == 0 -> de um tipo)
    constexpr double BBN_INIT_XP = 0.875;
    constexpr double BBN_INIT_XN = 0.125;

    // ── Regime 3: Photon Plasma / Recombination ──
    constexpr int    PLASMA_BARYON_COUNT = 1800;
    constexpr int    PLASMA_PHOTON_COUNT = 3200;
    constexpr int    PLASMA_GRID_SIZE = 64;
    constexpr int    PLASMA_HELIUM_RATIO_DIVISOR = 7;  // Partículas alfa: 1 a cada 7 bárions

    // ── Regime 4: Structure Formation ──
    constexpr int    STRUCT_ZELDOVICH_N_CBRT = 25;     // 25³ (gerará ~15.625 partículas)
    constexpr double STRUCT_BOX_SIZE_MPC = 50.0;       // Tamanho cúbico da simulação inicial
    constexpr int    STRUCT_GRID_SIZE = 64;
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
}
