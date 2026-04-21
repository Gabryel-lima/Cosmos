// src/physics/NBody.cpp — Solver N-corpos com octárvore Barnes-Hut.
#include <cmath>
#include <algorithm>
#include <limits>

#include "NBody.hpp"
#include "Constants.hpp"
#include "../core/CpuFeatures.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// SECÇÃO 1 — Declarações forward das implementações especializadas
//
// nbody_step_sse2  : definida abaixo (fallback escalar/SSE2, sempre presente)
// nbody_step_avx2  : definida em NBody_avx.cpp, compilado com -mavx2 -mfma.
//                    A declaração só existe quando o CMake confirma suporte.
// ─────────────────────────────────────────────────────────────────────────────

// Instância interna do solver Barnes-Hut compartilhada pelas duas impls.
// Thread-safety: NBody::step() não é reentrant; não há problema aqui.
static NBodySolver g_solver;

// Assinaturas usam NBodySolver& explicitamente para evitar globals ocultos
static void nbody_step_sse2(ParticlePool& pool, float dt);

#if defined(COSMOS_HAS_AVX2)
    void nbody_step_avx2(ParticlePool& pool, float dt);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// SECÇÃO 2 — Dispatcher (NBody é a fachada pública; NBodySolver é o motor)
//
// NBody::step() é o único ponto de entrada exposto ao resto do engine.
// Escolhe o caminho de execução uma vez por chamada (O(1) após 1ª consulta).
// ─────────────────────────────────────────────────────────────────────────────

void NBody::step(ParticlePool& pool, float dt) {
#if defined(COSMOS_HAS_AVX2)
    if (cpu::has_avx2()) {
        nbody_step_avx2(pool, dt);
        return;
    }
#endif
    nbody_step_sse2(pool, dt);   // fallback: sempre seguro em qualquer x86-64
}

// ─────────────────────────────────────────────────────────────────────────────
// SECÇÃO 3 — Octárvore Barnes-Hut (implementação)
//
// Compilado com flags base (-march=x86-64, sem AVX).
// NBody_avx.cpp pode reimplementar apenas os loops internos com intrinsics.
// ─────────────────────────────────────────────────────────────────────────────

static int octant(const OctreeNode& node, double x, double y, double z) {
    int oct = 0;
    if (x > node.cx) oct |= 1;
    if (y > node.cy) oct |= 2;
    if (z > node.cz) oct |= 4;
    return oct;
}

static OctreeNode makeChild(const OctreeNode& parent, int oct) {
    double h = parent.half * 0.5;
    OctreeNode child;
    child.cx   = parent.cx + ((oct & 1) ? h : -h);
    child.cy   = parent.cy + ((oct & 2) ? h : -h);
    child.cz   = parent.cz + ((oct & 4) ? h : -h);
    child.half = h;
    return child;
}

void NBodySolver::insertParticle(OctreeNode& node, int idx,
                                  const ParticlePool& pool, int depth)
{
    if (depth > 64) return;

    double m     = pool.mass[idx];
    double total = node.total_mass + m;

    node.com_x       = (node.com_x * node.total_mass + pool.x[idx] * m) / total;
    node.com_y       = (node.com_y * node.total_mass + pool.y[idx] * m) / total;
    node.com_z       = (node.com_z * node.total_mass + pool.z[idx] * m) / total;
    node.total_mass  = total;

    if (node.particle_index == -1 && !node.children[0]) {
        node.particle_index = idx;
        return;
    }

    if (node.particle_index >= 0) {
        int old_idx = node.particle_index;
        node.particle_index = -1;
        int oct_old = octant(node, pool.x[old_idx], pool.y[old_idx], pool.z[old_idx]);
        if (!node.children[oct_old])
            node.children[oct_old] = std::make_unique<OctreeNode>(makeChild(node, oct_old));
        insertParticle(*node.children[oct_old], old_idx, pool, depth + 1);
    }

    int oct = octant(node, pool.x[idx], pool.y[idx], pool.z[idx]);
    if (!node.children[oct])
        node.children[oct] = std::make_unique<OctreeNode>(makeChild(node, oct));
    insertParticle(*node.children[oct], idx, pool, depth + 1);
}

void NBodySolver::buildTree(const ParticlePool& pool) {
    if (pool.x.empty()) { root_.reset(); return; }

    double xmin =  std::numeric_limits<double>::max();
    double xmax =  std::numeric_limits<double>::lowest();
    double ymin = xmin, ymax = xmax, zmin = xmin, zmax = xmax;

    for (size_t i = 0; i < pool.x.size(); ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        xmin = std::min(xmin, pool.x[i]); xmax = std::max(xmax, pool.x[i]);
        ymin = std::min(ymin, pool.y[i]); ymax = std::max(ymax, pool.y[i]);
        zmin = std::min(zmin, pool.z[i]); zmax = std::max(zmax, pool.z[i]);
    }

    double half = std::max({xmax - xmin, ymax - ymin, zmax - zmin}) * 0.5 * 1.01;
    if (half <= 0.0) half = 1.0;

    root_     = std::make_unique<OctreeNode>();
    root_->cx = (xmin + xmax) * 0.5;
    root_->cy = (ymin + ymax) * 0.5;
    root_->cz = (zmin + zmax) * 0.5;
    root_->half = half;

    for (size_t i = 0; i < pool.x.size(); ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        if (pool.mass[i] <= 0.0) continue;
        insertParticle(*root_, static_cast<int>(i), pool, 0);
    }
}

void NBodySolver::computeForceFromNode(const OctreeNode& node, int target_idx,
                                        const ParticlePool& pool,
                                        double& ax, double& ay, double& az) const
{
    if (node.total_mass == 0.0) return;

    double dx = node.com_x - pool.x[target_idx];
    double dy = node.com_y - pool.y[target_idx];
    double dz = node.com_z - pool.z[target_idx];
    double r2 = dx*dx + dy*dy + dz*dz;

    if (r2 == 0.0) return;

    double node_size = node.half * 2.0;
    bool use_as_point =
        (node.particle_index >= 0 && node.particle_index != target_idx) ||
        (node_size * node_size < theta * theta * r2);

    if (use_as_point) {
        if (node.particle_index == target_idx) return;
        double r  = std::sqrt(r2 + softening * softening);
        double r3 = r * r * r;
        double a  = phys::G * node.total_mass / r3;
        ax += a * dx;
        ay += a * dy;
        az += a * dz;
    } else {
        for (int oct = 0; oct < 8; ++oct)
            if (node.children[oct])
                computeForceFromNode(*node.children[oct], target_idx,
                                     pool, ax, ay, az);
    }
}

void NBodySolver::computeForces(const ParticlePool& pool,
                                 std::vector<double>& ax,
                                 std::vector<double>& ay,
                                 std::vector<double>& az)
{
    size_t n = pool.x.size();
    ax.assign(n, 0.0);
    ay.assign(n, 0.0);
    az.assign(n, 0.0);

    buildTree(pool);
    if (!root_) return;

    for (size_t i = 0; i < n; ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        computeForceFromNode(*root_, static_cast<int>(i), pool, ax[i], ay[i], az[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SECÇÃO 4 — nbody_step_sse2: integrador completo, caminho SSE2/fallback
//
// Esta é a DEFINIÇÃO que o linker precisava e não encontrava antes.
// Usa g_solver (NBodySolver estático) e aplica integração Leapfrog.
// ─────────────────────────────────────────────────────────────────────────────

static void nbody_step_sse2(ParticlePool& pool, float dt) {
    const size_t n = pool.x.size();
    if (n == 0) return;

    // Vetores temporários de aceleração (double para precisão gravitacional)
    std::vector<double> ax, ay, az;
    g_solver.computeForces(pool, ax, ay, az);

    // Integração Leapfrog (kick-drift-kick): estável para órbitas de longa duração
    const double half_dt = dt * 0.5;

    for (size_t i = 0; i < n; ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;

        // Kick (½ passo de velocidade)
        pool.vx[i] += ax[i] * half_dt;
        pool.vy[i] += ay[i] * half_dt;
        pool.vz[i] += az[i] * half_dt;

        // Drift (passo de posição completo)
        pool.x[i] += pool.vx[i] * dt;
        pool.y[i] += pool.vy[i] * dt;
        pool.z[i] += pool.vz[i] * dt;
    }

    // Recalcular forças com posições atualizadas para 2º kick
    g_solver.computeForces(pool, ax, ay, az);

    for (size_t i = 0; i < n; ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;

        // Kick final (½ passo de velocidade)
        pool.vx[i] += ax[i] * half_dt;
        pool.vy[i] += ay[i] * half_dt;
        pool.vz[i] += az[i] * half_dt;
    }
}
