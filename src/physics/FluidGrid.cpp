// src/physics/FluidGrid.cpp
#include "FluidGrid.hpp"
#include "Constants.hpp"
#include <cmath>
#include <algorithm>

// ── Helpers ──────────────────────────────────────────────────────────────────

float FluidGrid::sample(const GridData& g, int x, int y, int z) {
    // Fronteiras periódicas
    x = ((x % g.NX) + g.NX) % g.NX;
    y = ((y % g.NY) + g.NY) % g.NY;
    z = ((z % g.NZ) + g.NZ) % g.NZ;
    return g.at(x, y, z);
}

float FluidGrid::laplacian(const GridData& g, int i, int j, int k, float dx2) {
    float c = g.at(i, j, k);
    return (sample(g,i+1,j,k) + sample(g,i-1,j,k)
          + sample(g,i,j+1,k) + sample(g,i,j-1,k)
          + sample(g,i,j,k+1) + sample(g,i,j,k-1) - 6.0f*c) / dx2;
}

// ── Solver de Poisson (Gauss-Seidel) ────────────────────────────────────────────────

void FluidGrid::solvePoisson(const Universe& universe,
                              std::vector<float>& phi_out,
                              double a, double mean_density)
{
    const GridData& delta = universe.density_field;
    int N = delta.NX;
    if (N == 0) return;
    phi_out.assign(delta.data.size(), 0.0f);

    float dx2 = (1.0f / static_cast<float>(N)) * (1.0f / static_cast<float>(N));
    float rhs_factor = static_cast<float>(4.0 * M_PI * phys::G * a * a * mean_density);

    // 20 iterações de Gauss-Seidel
    std::vector<float> phi_new = phi_out;
    for (int iter = 0; iter < 20; ++iter) {
        for (int k = 0; k < N; ++k)
        for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            float rhs = rhs_factor * delta.at(i, j, k);
            // Estêncil laplaciano: -6φ_ij + vizinhos = rhs * dx²
            //float sum = (sample(delta,i+1,j,k) + sample(delta,i-1,j,k)
            //           + sample(delta,i,j+1,k) + sample(delta,i,j-1,k)
            //           + sample(delta,i,j,k+1) + sample(delta,i,j,k-1));
            // Aguarde — phi, não delta. Usar phi_new para o estêncil:
            auto phi_get = [&](int x, int y, int z) -> float {
                x = ((x%N)+N)%N; y = ((y%N)+N)%N; z = ((z%N)+N)%N;
                return phi_new[x + N*(y + N*z)];
            };
            float phi_sum = phi_get(i+1,j,k) + phi_get(i-1,j,k)
                          + phi_get(i,j+1,k) + phi_get(i,j-1,k)
                          + phi_get(i,j,k+1) + phi_get(i,j,k-1);
            phi_new[i + N*(j + N*k)] = (phi_sum - rhs * dx2) / 6.0f;
        }
    }
    phi_out = phi_new;
}

double FluidGrid::baryonDensity(double a) {
    // n_b(a) = Ω_b * ρ_crit * a^{-3} / m_p
    constexpr double Omega_b = 0.049;  // parâmetro de densidade bariônica
    double rho_b = Omega_b * phys::rho_crit / (a * a * a);
    return rho_b / phys::m_p;  // densidade numérica [m⁻³]
}

// ── Equação de continuidade: ∂δ/∂t + (1/a)∇·[(1+δ)v] = 0 ───────────────────────

void FluidGrid::applyContinuity(Universe& universe, double dt, double a) {
    GridData& delta = universe.density_field;
    const GridData& vx = universe.velocity_x;
    const GridData& vy = universe.velocity_y;
    const GridData& vz = universe.velocity_z;
    int N = delta.NX;
    if (N == 0) return;
    float dx = 1.0f / static_cast<float>(N);
    float inv_a_dx = static_cast<float>(1.0 / (a * 2.0 * dx));  // 2dx para diferença centrada

    GridData delta_new = delta;
    for (int k = 0; k < N; ++k)
    for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) {
        float d = delta.at(i, j, k);
        // Divergência de (1+δ)v usando diferenças centradas
        auto get_d = [&](int x, int y, int z) -> float { return sample(delta, x, y, z); };
        auto get_vx = [&](int x, int y, int z) -> float { return sample(vx, x, y, z); };
        auto get_vy = [&](int x, int y, int z) -> float { return sample(vy, x, y, z); };
        auto get_vz = [&](int x, int y, int z) -> float { return sample(vz, x, y, z); };

        float div = ((1.0f + get_d(i+1,j,k)) * get_vx(i+1,j,k)
                   - (1.0f + get_d(i-1,j,k)) * get_vx(i-1,j,k)) * inv_a_dx
                  + ((1.0f + get_d(i,j+1,k)) * get_vy(i,j+1,k)
                   - (1.0f + get_d(i,j-1,k)) * get_vy(i,j-1,k)) * inv_a_dx
                  + ((1.0f + get_d(i,j,k+1)) * get_vz(i,j,k+1)
                   - (1.0f + get_d(i,j,k-1)) * get_vz(i,j,k-1)) * inv_a_dx;

        delta_new.at(i, j, k) = d - static_cast<float>(dt) * div;
    }
    delta = delta_new;
}

// ── Equação de Euler: ∂v/∂t + Hv + (1/a)(v·∇)v = -(1/a)∇φ ────────────────────

void FluidGrid::applyEuler(Universe& universe, const std::vector<float>& phi,
                            double dt, double a, double H, double c_s2)
{
    GridData& vx = universe.velocity_x;
    GridData& vy = universe.velocity_y;
    GridData& vz = universe.velocity_z;
    const GridData& delta = universe.density_field;
    int N = vx.NX;
    if (N == 0) return;
    float dx2 = static_cast<float>(2.0 / N);  // 2*dx para diferença centrada
    float dtf  = static_cast<float>(dt);
    float Hf   = static_cast<float>(H);
    float af   = static_cast<float>(a);

    auto phi_get = [&](int x, int y, int z) -> float {
        x = ((x%N)+N)%N; y = ((y%N)+N)%N; z = ((z%N)+N)%N;
        return phi[x + N*(y + N*z)];
    };

    GridData vx_new = vx, vy_new = vy, vz_new = vz;
    for (int k = 0; k < N; ++k)
    for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) {
        // Termo gravitacional: -∇φ/a
        float gx = -(phi_get(i+1,j,k) - phi_get(i-1,j,k)) / (af * dx2);
        float gy = -(phi_get(i,j+1,k) - phi_get(i,j-1,k)) / (af * dx2);
        float gz = -(phi_get(i,j,k+1) - phi_get(i,j,k-1)) / (af * dx2);

        // Termo de pressão da radiação: -∇(c_s² * δ) / a
        float px = -static_cast<float>(c_s2) *
                   (sample(delta,i+1,j,k) - sample(delta,i-1,j,k)) / (af * dx2);
        float py = -static_cast<float>(c_s2) *
                   (sample(delta,i,j+1,k) - sample(delta,i,j-1,k)) / (af * dx2);
        float pz = -static_cast<float>(c_s2) *
                   (sample(delta,i,j,k+1) - sample(delta,i,j,k-1)) / (af * dx2);

        vx_new.at(i,j,k) = vx.at(i,j,k) * (1.0f - Hf*dtf) + dtf*(gx + px);
        vy_new.at(i,j,k) = vy.at(i,j,k) * (1.0f - Hf*dtf) + dtf*(gy + py);
        vz_new.at(i,j,k) = vz.at(i,j,k) * (1.0f - Hf*dtf) + dtf*(gz + pz);
    }
    vx = vx_new; vy = vy_new; vz = vz_new;
}

// ── Passo principal ───────────────────────────────────────────────────────────

void FluidGrid::step(Universe& universe, double cosmic_dt, double a, double H, double T_keV)
{
    // Velocidade do som BAO: c_s² = c²/3 (dominada pela radiação)
    // Após a recombinação, c_s ≈ 0
    double T_K = T_keV * phys::keV / phys::kB;
    double c_s2 = (T_K > 3000.0) ? (phys::c * phys::c / 3.0) : 0.0;

    double mean_density = baryonDensity(a) * phys::m_p;  // [kg/m³]

    std::vector<float> phi;
    solvePoisson(universe, phi, a, mean_density);

    applyContinuity(universe, cosmic_dt, a);
    applyEuler(universe, phi, cosmic_dt, a, H, c_s2);
}
