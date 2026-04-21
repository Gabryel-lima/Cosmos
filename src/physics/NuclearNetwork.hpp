#pragma once
// src/physics/NuclearNetwork.hpp — Rede de reações da Nucleossíntese do Big Bang.
// Rede de Wagoner simplificada rastreando n, p, D, He3, He4, Li7 frações numéricas.

#include "../core/Universe.hpp"

class NuclearNetwork {
public:
    /// Avançar as abundâncias em um passo de tempo cósmico.
    /// @param ab       entrada/saída: abundâncias nucleares
    /// @param dt       passo de tempo cósmico [s]
    /// @param T_keV    temperatura atual [keV]
    /// @param a        fator de escala (para densidade bariônica)
    void step(NuclearAbundances& ab, double dt, double T_keV, double a);

    /// Inicializar abundâncias para equilíbrio térmico em T_keV.
    static NuclearAbundances equilibriumAbundances(double T_keV);

private:
    // Taxas de reação dependentes de temperatura [cm³/mol/s ou s⁻¹]
    static double rate_n_decay(double T_keV);           // n → p + e + ν
    static double rate_np_to_D(double T_keV, double nb);// n + p → D + γ
    static double rate_D_to_np(double T_keV, double nb);// D → n + p (photo-dissociation)
    static double rate_DD_to_He3n(double T_keV);        // D + D → He3 + n
    static double rate_DD_to_He4(double T_keV);         // D + D → He4 (approx)
    static double rate_He3n_to_tp(double T_keV);        // He3 + n → t + p
    static double rate_tp_to_He4(double T_keV);         // t + p → He4

    static double baryon_density_cgs(double a);  // [cm⁻³]
};
