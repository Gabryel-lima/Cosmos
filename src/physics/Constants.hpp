#pragma once
// src/physics/Constants.hpp — Constantes físicas em unidades SI.
// Toda a física usa estas. Converter para unidades de exibição apenas na renderização.

namespace phys {
    // Constantes fundamentais
    constexpr double G       = 6.674e-11;    // constante gravitacional [m³/kg/s²]
    constexpr double c       = 2.998e8;      // velocidade da luz [m/s]
    constexpr double kB      = 1.381e-23;    // constante de Boltzmann [J/K]
    constexpr double hbar    = 1.055e-34;    // constante de Planck reduzida [J·s]
    constexpr double m_p     = 1.673e-27;    // massa do próton [kg]
    constexpr double m_n     = 1.675e-27;    // massa do nêutron [kg]
    constexpr double m_e     = 9.109e-31;    // massa do elétron [kg]
    constexpr double eV      = 1.602e-19;    // 1 eV em Joules
    constexpr double keV     = 1.602e-16;    // 1 keV em Joules
    constexpr double MeV     = 1.602e-13;    // 1 MeV em Joules

    // Parâmetros cosmológicos (Planck 2018)
    constexpr double H0      = 2.184e-18;    // constante de Hubble [s⁻¹] (67.4 km/s/Mpc)
    constexpr double Omega_r = 9.24e-5;      // parâmetro de densidade de radiação
    constexpr double Omega_m = 0.315;        // parâmetro de densidade de matéria
    constexpr double Omega_L = 0.685;        // parâmetro de densidade de energia escura
    constexpr double T_CMB   = 2.725;        // CMB temperature today [K]
    constexpr double Mpc     = 3.086e22;     // 1 Megaparsec in meters

    // Derivadas
    constexpr double T_CMB_keV = T_CMB * kB / keV;  // ~2.35e-10 keV

    // Densidade crítica hoje: ρ_crit = 3H0²/(8πG)
    constexpr double rho_crit = 3.0 * H0 * H0 / (8.0 * 3.14159265358979323846 * G);

    // Conversões de unidades
    constexpr double yr_to_s  = 3.15576e7;  // segundos por ano
    constexpr double Gyr_to_s = 3.15576e16; // segundos por gigaano
    constexpr double kpc      = 3.086e19;   // 1 kiloparsec em metros
    constexpr double fm       = 1.0e-15;    // 1 femtômetro em metros

    // Tempo de fim da simulação
    constexpr double t_today  = 13.8e9 * yr_to_s; // ~4.35e17 s

    // Conversão de temperatura para keV: T[keV] = T[K] * kB / keV
    inline constexpr double K_to_keV(double T_kelvin) {
        return T_kelvin * kB / keV;
    }
    inline constexpr double keV_to_K(double T_keV) {
        return T_keV * keV / kB;
    }
} // namespace phys
