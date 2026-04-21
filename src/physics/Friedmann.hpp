#pragma once
// src/physics/Friedmann.hpp — Solver da equação de Friedmann ΛCDM
// Calcula o fator de escala a(t) e quantidades derivadas via integração RK4.

namespace phys {

/// Calcular H(a): taxa de Hubble em função do fator de escala.
/// H²(a) = H₀²[Ω_r·a⁻⁴ + Ω_m·a⁻³ + Ω_Λ]
double hubble_from_scale(double a);

/// Integrar da/dt = a·H(a) usando RK4 para encontrar a(t + dt).
double integrate_scale_factor(double a_current, double dt);

/// Temperatura a partir do fator de escala: T(a) = T_CMB / a  [Kelvin]
double temperature_from_scale(double a);

/// Temperatura a partir do fator de escala em keV
double temperature_keV_from_scale(double a);

/// Encontrar o fator de escala para uma dada temperatura (keV) — busca binária.
double scale_at_temperature_keV(double T_keV);

/// Encontrar o tempo cósmico para um dado fator de escala (integra a partir de a≈0).
/// Custoso — chamar apenas na inicialização, não a cada quadro.
double cosmic_time_from_scale(double a_target);

} // namespace phys
