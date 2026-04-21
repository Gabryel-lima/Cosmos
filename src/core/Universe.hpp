#pragma once
// src/core/Universe.hpp — Contêiner de estado de nível superior compartilhado por todos os sistemas.

#include "../physics/ParticlePool.hpp"
#include <array>
#include <vector>
#include <cstdint>

/// Grade 3D plana de valores escalares (campo de densidade, grade de fluido, etc.)
struct GridData {
    std::vector<float> data;   // linha-principal, índice = x + NX*(y + NY*z)
    int NX = 0, NY = 0, NZ = 0;

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
    double Xn  = 0.125;  // nêutron
    double Xp  = 0.875;  // próton/hidrogênio
    double Xd  = 0.0;    // deutério
    double Xhe3= 0.0;    // He-3
    double Xhe4= 0.0;    // He-4
    double Xli7= 0.0;    // Li-7
};

/// Instantâneo de câmera para transferência entre regimes.
struct CameraState {
    double pos_x = 0.0, pos_y = 0.0, pos_z = 5.0;
    float  fwd_x = 0.0f, fwd_y = 0.0f, fwd_z = -1.0f;
    double zoom  = 100.0;
    bool   ortho = false;
};

/// Estado completo da simulação. Instância única, propriedade de main().
struct Universe {
    ParticlePool    particles;
    GridData        density_field;  // campo 3D de densidade/escalar
    GridData        velocity_x;     // componentes de velocidade do fluido
    GridData        velocity_y;
    GridData        velocity_z;
    NuclearAbundances abundances;

    // Perfil de qualidade (definido pelo sinalizador --quality)
    struct QualityProfile {
        int   N_particles    = 100'000;
        int   grid_res       = 64;
        float barnes_hut_theta = 0.5f;
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
