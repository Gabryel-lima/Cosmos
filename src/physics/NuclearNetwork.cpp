// src/physics/NuclearNetwork.cpp — Rede de reações BBN (Wagoner simplificado).
#include "NuclearNetwork.hpp"
#include "Constants.hpp"
#include <cmath>
#include <algorithm>

// ── Reaction rates (physical approximations) ─────────────────────────────────

// Tempo de vida do nêtron: τ_n = 880 s
double NuclearNetwork::rate_n_decay(double /*T_keV*/) {
    return 1.0 / 880.0;  // [s⁻¹]
}

double NuclearNetwork::baryon_density_cgs(double a) {
    // η = 6.1e-10 (proporção bárion-fóton)
    // n_b(a) = n_b0 / a^3, n_b0 ≈ 0.25 m⁻³ = 0.25e-6 cm⁻³
    constexpr double n_b0_cgs = 0.25e-6;  // bárions/cm³ em a=1
    return n_b0_cgs / (a * a * a);
}

// n + p → D + γ  taxa ≈ 4.55e4 * T9^(-2/3) exp(-2.515*T9^(-1/3)) [cm³/s/mol]
// T9 = T [keV] / (kB em keV) * 1e-9 K = T_keV / 86.17 keV * 1e-9 ... simplificando:
// T9 ≈ T_keV / 86.17 (pois 1 MeV/kB = 11.6e9 K => T9 = T_MeV * 11.6)
static double T9_from_keV(double T_keV) { return T_keV / 86170.0; }  // 1 keV = 1,16e7 K

double NuclearNetwork::rate_np_to_D(double T_keV, double nb) {
    double T9 = T9_from_keV(T_keV);
    if (T9 <= 0.0) return 0.0;
    double T9_13 = std::cbrt(T9);
    double rate_cm3_mol_s = 4.55e4 * std::pow(T9, -2.0/3.0)
                          * std::exp(-2.515 / T9_13);
    // Converter para s⁻¹: multiplicar por nb (cm⁻³) / Avogadro
    constexpr double NA = 6.022e23;
    return rate_cm3_mol_s * nb / NA;
}

double NuclearNetwork::rate_D_to_np(double T_keV, double nb) {
    // Balanço detalhado: taxa_volta = taxa_frente * exp(Q/kT) * (fatores estatísticos)
    // Q(formação de D) ≈ 2.224 MeV = 2224 keV
    if (T_keV <= 0.0) return 0.0;
    double fwd = rate_np_to_D(T_keV, nb);
    double exp_factor = std::exp(-2224.0 / T_keV);
    return fwd * exp_factor * 1e20;  // prefator aproximado
}

double NuclearNetwork::rate_DD_to_He3n(double T_keV) {
    double T9 = T9_from_keV(T_keV);
    if (T9 <= 0.0) return 0.0;
    return 3.9e8 * std::pow(T9, -2.0/3.0) * std::exp(-4.258 / std::cbrt(T9));
}

double NuclearNetwork::rate_DD_to_He4(double T_keV) {
    return rate_DD_to_He3n(T_keV) * 0.5;  // razão de ramificação aproximada
}

double NuclearNetwork::rate_He3n_to_tp(double T_keV) {
    double T9 = T9_from_keV(T_keV);
    if (T9 <= 0.0) return 0.0;
    return 7.06e13 * std::pow(T9, -2.0/3.0);
}

double NuclearNetwork::rate_tp_to_He4(double T_keV) {
    double T9 = T9_from_keV(T_keV);
    if (T9 <= 0.0) return 0.0;
    return 2.04e13 * std::pow(T9, -2.0/3.0) * std::exp(-4.524 / std::cbrt(T9));
}

// ── Inicialização de equilíbrio ────────────────────────────────────────────────

NuclearAbundances NuclearNetwork::equilibriumAbundances(double T_keV) {
    NuclearAbundances ab;
    // proporção n/p de Boltzmann: n/p = exp(-Q/kT), Q = (m_n - m_p)c² ≈ 1.293 MeV
    double Q_keV = 1293.0;
    double np_ratio = std::exp(-Q_keV / std::max(T_keV, 0.001));
    np_ratio = std::clamp(np_ratio, 0.0, 1.0);
    ab.Xn = np_ratio / (1.0 + np_ratio);
    ab.Xp = 1.0 / (1.0 + np_ratio);
    ab.Xd = 0.0; ab.Xhe3 = 0.0; ab.Xhe4 = 0.0; ab.Xli7 = 0.0;
    return ab;
}

// ── Passo ODE (Euler direto simples com limitação) ──────────────────────────────

void NuclearNetwork::step(NuclearAbundances& ab, double dt, double T_keV, double a) {
    double nb = baryon_density_cgs(a);
    constexpr double NA = 6.022e23;

    // Taxas de reação [s⁻¹]
    double l_nd    = rate_n_decay(T_keV);                   // n → p
    double l_npD   = rate_np_to_D(T_keV, nb);              // n+p → D (por núcleon)
    double l_Dnp   = rate_D_to_np(T_keV, nb);              // D → n+p
    double l_DD3   = rate_DD_to_He3n(T_keV) * nb / NA;     // D+D → He3+n
    double l_DD4   = rate_DD_to_He4(T_keV)  * nb / NA;     // D+D → He4
    double l_3n    = rate_He3n_to_tp(T_keV) * nb / NA;     // He3+n → T+p (→He4)
    double l_tp    = rate_tp_to_He4(T_keV)  * nb / NA;     // t+p → He4

    double Xn = ab.Xn, Xp = ab.Xp, Xd = ab.Xd;
    double Xhe3 = ab.Xhe3, Xhe4 = ab.Xhe4;

    // dXn/dt = -l_nd*Xn - l_npD*Xn*Xp*nb + l_Dnp*Xd
    double dXn = (-l_nd * Xn
                  - l_npD * Xn * Xp * nb / NA
                  + l_Dnp * Xd) * dt;

    double dXd = (l_npD * Xn * Xp * nb / NA
                  - l_Dnp * Xd
                  - 2.0*l_DD3 * Xd*Xd
                  - 2.0*l_DD4 * Xd*Xd) * dt;

    double dXhe3 = (l_DD3 * Xd*Xd - l_3n * Xhe3 * Xn) * dt;
    double dXhe4 = (l_DD4 * Xd*Xd + l_3n * Xhe3 * Xn + l_tp * 0.01) * dt;
    double dXp   = (-dXn - dXd - dXhe3 - dXhe4); // conservação

    // Aplicar com limitação
    ab.Xn   = std::clamp(Xn   + dXn,   0.0, 1.0);
    ab.Xd   = std::clamp(Xd   + dXd,   0.0, 1.0);
    ab.Xhe3 = std::clamp(Xhe3 + dXhe3, 0.0, 1.0);
    ab.Xhe4 = std::clamp(Xhe4 + dXhe4, 0.0, 1.0);
    ab.Xp   = std::clamp(Xp   + dXp,   0.0, 1.0);

    // Renormalizar
    double sum = ab.Xn + ab.Xp + ab.Xd + ab.Xhe3 + ab.Xhe4 + ab.Xli7;
    if (sum > 0.0) {
        double inv = 1.0 / sum;
        ab.Xn *= inv; ab.Xp *= inv; ab.Xd *= inv;
        ab.Xhe3 *= inv; ab.Xhe4 *= inv; ab.Xli7 *= inv;
    }
}
