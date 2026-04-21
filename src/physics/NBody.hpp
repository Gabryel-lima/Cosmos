#pragma once
// src/physics/NBody.hpp — Solver gravitacional N-corpos Barnes-Hut O(N log N).
// Constrói uma octárvore a cada quadro, depois acumula forças.

#include "ParticlePool.hpp"
#include <vector>
#include <memory>

struct OctreeNode {
    // Caixa delimitadora
    double cx, cy, cz;   // centro
    double half;         // meia-largura

    // Centro de massa / massa total para multipolo Barnes-Hut
    double com_x = 0.0, com_y = 0.0, com_z = 0.0;
    double total_mass = 0.0;

    // Nó folha (particle_index >= 0) ou nó interno (filhos definidos)
    int particle_index = -1;  // -1 = nó interno
    std::unique_ptr<OctreeNode> children[8];

    bool is_leaf() const { return particle_index >= 0 || total_mass == 0.0; }
};

/// Solver gravitacional N-corpos Barnes-Hut.
class NBodySolver {
public:
    float theta      = 0.5f;   // parâmetro de ângulo de abertura
    float softening  = 1e-3f;  // comprimento de suavização (unidades da simulação)

    /// Calcular acelerações gravitacionais para todas as partículas ativas.
    /// Resultados escritos em ax[], ay[], az[] (dimensionados ao número de partículas).
    void computeForces(const ParticlePool& pool,
                       std::vector<double>& ax,
                       std::vector<double>& ay,
                       std::vector<double>& az);

private:
    void buildTree(const ParticlePool& pool);
    void insertParticle(OctreeNode& node, int idx,
                        const ParticlePool& pool, int depth);
    void computeForceFromNode(const OctreeNode& node, int target_idx,
                              const ParticlePool& pool,
                              double& ax, double& ay, double& az) const;

    std::unique_ptr<OctreeNode> root_;
};
