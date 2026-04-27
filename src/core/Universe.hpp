#pragma once
// src/core/Universe.hpp — Contêiner de estado de nível superior compartilhado por todos os sistemas.

#include "../physics/ParticlePool.hpp"
#include "RegimeConfig.hpp"
#include <cstddef>

#include <array>
#include <vector>
#include <cstdint>

/// Grade 3D plana de valores escalares (campo de densidade, grade de fluido, etc.)
struct GridData {
    std::vector<float> data;   // linha-principal, índice = x + NX*(y + NY*z)
    int NX = 0, NY = 0, NZ = 0;

    // Defaults mapped from RegimeConfig (no automatic allocation).
    static constexpr int DEFAULT_NX = RegimeConfig::GridDataDefaults::default_NX;
    static constexpr int DEFAULT_NY = RegimeConfig::GridDataDefaults::default_NY;
    static constexpr int DEFAULT_NZ = RegimeConfig::GridDataDefaults::default_NZ;

    void resize(int nx, int ny, int nz) {
        NX = nx; NY = ny; NZ = nz;
        data.assign(static_cast<size_t>(nx * ny * nz), 0.0f);
    }
    float& at(int x, int y, int z)       { return data[x + NX*(y + NY*z)]; }
    float  at(int x, int y, int z) const { return data[x + NX*(y + NY*z)]; }
    void clear() { std::fill(data.begin(), data.end(), 0.0f); }
};

/// Abundâncias nucleares (frações numéricas, soma ≈ 1 para bárions).
struct NuclearAbundances {
    double Xn  = RegimeConfig::BBN_INIT_XN;  // nêutron
    double Xp  = RegimeConfig::BBN_INIT_XP;  // próton/hidrogênio
    double Xd  = 0.0;    // deutério
    double Xhe3= 0.0;    // He-3
    double Xhe4= 0.0;    // He-4
    double Xli7= 0.0;    // Li-7
    // Mirrors in RegimeConfig for convenience
    static constexpr double DEFAULT_Xp = RegimeConfig::NuclearAbundances::Xp;
    static constexpr double DEFAULT_Xn = RegimeConfig::NuclearAbundances::Xn;
    static constexpr int    DEFAULT_NUCLEON_COUNT = RegimeConfig::NuclearAbundances::NucleonCount;
    static constexpr int    DEFAULT_PROTON_RATIO = RegimeConfig::NuclearAbundances::ProtonRatio;
};

/// Instantâneo de câmera para transferência entre regimes.
struct CameraState {
    double pos_x = RegimeConfig::CameraState::pos_x;
    double pos_y = RegimeConfig::CameraState::pos_y;
    double pos_z = RegimeConfig::CameraState::pos_z;
    float  fwd_x = RegimeConfig::CameraState::fwd_x;
    float  fwd_y = RegimeConfig::CameraState::fwd_y;
    float  fwd_z = RegimeConfig::CameraState::fwd_z;
    double zoom  = RegimeConfig::CameraState::zoom;
    bool   ortho = RegimeConfig::CameraState::ortho;

    static constexpr double DEFAULT_POS_X = RegimeConfig::CameraState::pos_x;
    static constexpr double DEFAULT_POS_Y = RegimeConfig::CameraState::pos_y;
    static constexpr double DEFAULT_POS_Z = RegimeConfig::CameraState::pos_z;
    static constexpr float  DEFAULT_FWD_X = RegimeConfig::CameraState::fwd_x;
    static constexpr float  DEFAULT_FWD_Y = RegimeConfig::CameraState::fwd_y;
    static constexpr float  DEFAULT_FWD_Z = RegimeConfig::CameraState::fwd_z;
    static constexpr double DEFAULT_ZOOM  = RegimeConfig::CameraState::zoom;
    static constexpr bool   DEFAULT_ORTHO = RegimeConfig::CameraState::ortho;
};

/// Estado completo da simulação. Instância única, propriedade de main().
struct Universe {
    ParticlePool    particles;
    GridData        density_field;  // campo 3D de densidade/escalar
    GridData        velocity_x;     // componentes de velocidade do fluido
    GridData        velocity_y;
    GridData        velocity_z;
    NuclearAbundances abundances;

    // Perfil de qualidade efetivo definido no build (QUALITY=...)
    struct QualityProfile {
        int   N_particles      = RegimeConfig::QualityProfile::N_particles;
        int   grid_res         = RegimeConfig::QualityProfile::grid_res;
        float barnes_hut_theta = RegimeConfig::QualityProfile::barnes_hut_theta;
    } quality;

    // Estado da simulação
    double scale_factor    = 1.0;
    double temperature_keV = 1e16;
    double cosmic_time     = 0.0;
    int    regime_index    = 0;

    // Específico da inflação: campo escalar 2D φ(x,y)
    std::vector<float> phi_field;      // 256*256 floats
    std::vector<float> phi_dot_field;
    int phi_NX = 256, phi_NY = 256;
    float inflate_3d_t = 0.0f;        // progresso da extrusão 3D: 0→1

    // Estatísticas de desempenho (atualizadas pelo renderizador)
    float fps              = 0.0f;
    float gpu_time_ms      = 0.0f;
    int   active_particles = 0;
};
