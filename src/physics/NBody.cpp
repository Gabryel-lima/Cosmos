// src/physics/NBody.cpp — Solver N-corpos com octárvore Barnes-Hut.
#include "NBody.hpp"
#include "Constants.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

// ── Construção da octárvore ───────────────────────────────────────────────────────

static int octant(const OctreeNode& node, double x, double y, double z) {
    int oct = 0;
    if (x > node.cx) oct |= 1;
    if (y > node.cy) oct |= 2;
    if (z > node.cz) oct |= 4;
    return oct;
}

static OctreeNode makeChild(const OctreeNode& parent, int oct) {
    double h = parent.half * 0.5;
    double cx = parent.cx + ((oct & 1) ? h : -h);
    double cy = parent.cy + ((oct & 2) ? h : -h);
    double cz = parent.cz + ((oct & 4) ? h : -h);
    OctreeNode child;
    child.cx = cx; child.cy = cy; child.cz = cz;
    child.half = h;
    return child;
}

void NBodySolver::insertParticle(OctreeNode& node, int idx,
                                  const ParticlePool& pool, int depth)
{
    if (depth > 64) return; // proteção contra recursão infinita

    double m = pool.mass[idx];
    // Atualizar centro de massa
    double total = node.total_mass + m;
    node.com_x = (node.com_x * node.total_mass + pool.x[idx] * m) / total;
    node.com_y = (node.com_y * node.total_mass + pool.y[idx] * m) / total;
    node.com_z = (node.com_z * node.total_mass + pool.z[idx] * m) / total;
    node.total_mass = total;

    if (node.particle_index == -1 && !node.children[0]) {
        // Folha vazia → armazenar partícula
        node.particle_index = idx;
        return;
    }

    if (node.particle_index >= 0) {
        // Folha ocupada → subdividir: reinserir partícula antiga
        int old_idx = node.particle_index;
        node.particle_index = -1;
        int oct_old = octant(node, pool.x[old_idx], pool.y[old_idx], pool.z[old_idx]);
        if (!node.children[oct_old]) {
            node.children[oct_old] = std::make_unique<OctreeNode>(makeChild(node, oct_old));
        }
        insertParticle(*node.children[oct_old], old_idx, pool, depth + 1);
    }

    // Inserir nova partícula
    int oct = octant(node, pool.x[idx], pool.y[idx], pool.z[idx]);
    if (!node.children[oct]) {
        node.children[oct] = std::make_unique<OctreeNode>(makeChild(node, oct));
    }
    insertParticle(*node.children[oct], idx, pool, depth + 1);
}

void NBodySolver::buildTree(const ParticlePool& pool) {
    if (pool.x.empty()) { root_.reset(); return; }

    // Encontrar caixa delimitadora
    double xmin = std::numeric_limits<double>::max();
    double xmax = std::numeric_limits<double>::lowest();
    double ymin = xmin, ymax = xmax, zmin = xmin, zmax = xmax;

    for (size_t i = 0; i < pool.x.size(); ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        xmin = std::min(xmin, pool.x[i]); xmax = std::max(xmax, pool.x[i]);
        ymin = std::min(ymin, pool.y[i]); ymax = std::max(ymax, pool.y[i]);
        zmin = std::min(zmin, pool.z[i]); zmax = std::max(zmax, pool.z[i]);
    }

    double half = std::max({xmax - xmin, ymax - ymin, zmax - zmin}) * 0.5 * 1.01;
    if (half <= 0.0) half = 1.0;

    root_ = std::make_unique<OctreeNode>();
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

// ── Cálculo de força ─────────────────────────────────────────────────────────

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

    // Critério de Barnes-Hut: usar nó como massa pontual se (tamanho_nó/r) < theta
    double node_size = node.half * 2.0;
    bool use_as_point = (node.particle_index >= 0 && node.particle_index != target_idx)
                     || (node_size * node_size < theta * theta * r2);

    if (use_as_point) {
        // Ignorar auto-interação
        if (node.particle_index == target_idx) return;
        double r = std::sqrt(r2 + softening * softening);
        double r3 = r * r * r;
        double a = phys::G * node.total_mass / r3;
        ax += a * dx;
        ay += a * dy;
        az += a * dz;
    } else {
        // Recursão nos filhos
        for (int oct = 0; oct < 8; ++oct) {
            if (node.children[oct]) {
                computeForceFromNode(*node.children[oct], target_idx,
                                     pool, ax, ay, az);
            }
        }
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
