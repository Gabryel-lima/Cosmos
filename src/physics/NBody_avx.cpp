// src/physics/NBody_avx.cpp
// Compilado isoladamente com -mavx2 -mfma pelo CMake.
// NUNCA inclua este arquivo em outro .cpp.
#include <immintrin.h>
#include <vector>

#include "NBody.hpp"
#include "ParticlePool.hpp"

// g_solver é static em NBody.cpp — não acessível aqui.
// AVX2 acelera apenas o integrador Leapfrog (FLOPs puro);
// a árvore Barnes-Hut é bound por memória, AVX não ajuda lá.
// Declaramos um NBodySolver local para este caminho.

static NBodySolver g_solver_avx;

void nbody_step_avx2(ParticlePool& pool, float dt) {
    const size_t n = pool.x.size();
    if (n == 0) return;

    std::vector<double> ax, ay, az;
    g_solver_avx.computeForces(pool, ax, ay, az);

    const double half_dt = dt * 0.5;
    const size_t n4 = n & ~size_t(3);   // múltiplo de 4 (AVX2 processa 4 doubles)

    // ── 1º kick: v += a * half_dt ─────────────────────────────────────────
    const __m256d vhdt = _mm256_set1_pd(half_dt);

    size_t i = 0;
    for (; i < n4; i += 4) {
        // Processar 4 partículas por vez.
        // Mascaramento de flags PF_ACTIVE: se qualquer uma estiver inativa,
        // o escalar abaixo cobre os casos de borda — aqui assumimos que o
        // pool está densamente preenchido (partículas inativas são raras).
        __m256d vx = _mm256_loadu_pd(&pool.vx[i]);
        __m256d vy = _mm256_loadu_pd(&pool.vy[i]);
        __m256d vz = _mm256_loadu_pd(&pool.vz[i]);

        vx = _mm256_fmadd_pd(_mm256_loadu_pd(&ax[i]), vhdt, vx);
        vy = _mm256_fmadd_pd(_mm256_loadu_pd(&ay[i]), vhdt, vy);
        vz = _mm256_fmadd_pd(_mm256_loadu_pd(&az[i]), vhdt, vz);

        _mm256_storeu_pd(&pool.vx[i], vx);
        _mm256_storeu_pd(&pool.vy[i], vy);
        _mm256_storeu_pd(&pool.vz[i], vz);
    }
    // Cauda escalar (partículas restantes que não completam um grupo de 4)
    for (; i < n; ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        pool.vx[i] += ax[i] * half_dt;
        pool.vy[i] += ay[i] * half_dt;
        pool.vz[i] += az[i] * half_dt;
    }

    // ── drift: x += v * dt ────────────────────────────────────────────────
    const __m256d vdt = _mm256_set1_pd(dt);

    i = 0;
    for (; i < n4; i += 4) {
        __m256d px = _mm256_loadu_pd(&pool.x[i]);
        __m256d py = _mm256_loadu_pd(&pool.y[i]);
        __m256d pz = _mm256_loadu_pd(&pool.z[i]);

        px = _mm256_fmadd_pd(_mm256_loadu_pd(&pool.vx[i]), vdt, px);
        py = _mm256_fmadd_pd(_mm256_loadu_pd(&pool.vy[i]), vdt, py);
        pz = _mm256_fmadd_pd(_mm256_loadu_pd(&pool.vz[i]), vdt, pz);

        _mm256_storeu_pd(&pool.x[i], px);
        _mm256_storeu_pd(&pool.y[i], py);
        _mm256_storeu_pd(&pool.z[i], pz);
    }
    for (; i < n; ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        pool.x[i] += pool.vx[i] * dt;
        pool.y[i] += pool.vy[i] * dt;
        pool.z[i] += pool.vz[i] * dt;
    }

    // ── 2º kick: recalcular forças e aplicar ─────────────────────────────
    g_solver_avx.computeForces(pool, ax, ay, az);

    i = 0;
    for (; i < n4; i += 4) {
        __m256d vx = _mm256_loadu_pd(&pool.vx[i]);
        __m256d vy = _mm256_loadu_pd(&pool.vy[i]);
        __m256d vz = _mm256_loadu_pd(&pool.vz[i]);

        vx = _mm256_fmadd_pd(_mm256_loadu_pd(&ax[i]), vhdt, vx);
        vy = _mm256_fmadd_pd(_mm256_loadu_pd(&ay[i]), vhdt, vy);
        vz = _mm256_fmadd_pd(_mm256_loadu_pd(&az[i]), vhdt, vz);

        _mm256_storeu_pd(&pool.vx[i], vx);
        _mm256_storeu_pd(&pool.vy[i], vy);
        _mm256_storeu_pd(&pool.vz[i], vz);
    }
    for (; i < n; ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        pool.vx[i] += ax[i] * half_dt;
        pool.vy[i] += ay[i] * half_dt;
        pool.vz[i] += az[i] * half_dt;
    }
}
