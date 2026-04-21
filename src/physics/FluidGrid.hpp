#pragma once
// src/physics/FluidGrid.hpp — Fluido Euleriano em uma grade 3D (coordenadas comóveis).
// Usado no Regime 3 (plasma bárion/MO + ondas sonoras BAO).

#include "../core/Universe.hpp"

class FluidGrid {
public:
    /// Avançar o fluido bariônico em um passo de tempo cósmico.
    /// Aplica equações de continuidade + Euler com termo de pressão de radiação.
    /// @param universe  entrada/saída: density_field, velocity_{x,y,z}
    /// @param cosmic_dt  passo de tempo [s]
    /// @param a          fator de escala
    /// @param H          taxa de Hubble [s⁻¹]
    /// @param T_keV      temperatura [keV]
    void step(Universe& universe, double cosmic_dt, double a, double H, double T_keV);

    /// Resolver equação de Poisson ∇²φ = 4πG a² ρ̄ δ via iterações de Gauss-Seidel.
    /// Armazena o potencial gravitacional em phi_out (dimensionado para a grade).
    void solvePoisson(const Universe& universe,
                      std::vector<float>& phi_out,
                      double a, double mean_density);

    /// Calcular densidade numérica de bárions a partir de parâmetros cosmológicos.
    static double baryonDensity(double a);

private:
    void applyContinuity(Universe& universe, double dt, double a);
    void applyEuler(Universe& universe, const std::vector<float>& phi,
                    double dt, double a, double H, double c_s2);

    static float sample(const GridData& g, int x, int y, int z);
    static float laplacian(const GridData& g, int i, int j, int k, float dx2);
};
