// src/physics/Friedmann.cpp
#include "Friedmann.hpp"
#include "Constants.hpp"
#include <cmath>

namespace phys {

double hubble_from_scale(double a) {
    if (a <= 0.0) return 1e60;  // Proteção contra singularidade do Big Bang
    double a2 = a * a;
    double a3 = a2 * a;
    double a4 = a3 * a;
    double H2 = H0 * H0 * (Omega_r / a4 + Omega_m / a3 + Omega_L);
    return std::sqrt(H2);
}

double integrate_scale_factor(double a_current, double dt) {
    if (a_current <= 0.0 || dt == 0.0) return a_current;

    // In the radiation-dominated era (a << 1) the exact analytical solution is:
    //   a(t)² = a₀² + 2·H₀·√Ω_r · dt
    // This avoids RK4 blow-up at the extreme time steps of the early universe.
    {
        double a2 = a_current * a_current;
        double a4 = a2 * a2;
        double a3 = a2 * a_current;
        double term_r = Omega_r / a4;
        double term_m = Omega_m / a3;
        if (term_r > 100.0 * (term_m + Omega_L)) {
            double a_new_sq = a2 + 2.0 * H0 * std::sqrt(Omega_r) * dt;
            if (a_new_sq > 0.0) return std::sqrt(a_new_sq);
        }
    }

    // General case: RK4  da/dt = a · H(a)
    auto f = [](double a) -> double {
        return a * hubble_from_scale(a);
    };
    double k1 = f(a_current);
    double k2 = f(a_current + 0.5 * dt * k1);
    double k3 = f(a_current + 0.5 * dt * k2);
    double k4 = f(a_current + dt * k3);
    return a_current + (dt / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
}

double temperature_from_scale(double a) {
    if (a <= 0.0) return 1e30;
    return T_CMB / a;
}

double temperature_keV_from_scale(double a) {
    return K_to_keV(temperature_from_scale(a));
}

double scale_at_temperature_keV(double T_keV) {
    // T(a) = T_CMB/a => a = T_CMB_keV / T_keV
    return T_CMB_keV / T_keV;
}

double cosmic_time_from_scale(double a_target) {
    // Integrar t(a) = ∫₀^a da' / (a' * H(a'))
    // Usar passos logarítmicos para estabilidade numérica em a pequeno
    if (a_target <= 0.0) return 0.0;
    if (a_target >= 1.0) {
        // Aproximação da idade atual
        return 13.8e9 * yr_to_s;
    }

    const int N = 10000;
    double log_a_min = -100.0; // a ~ e^-100 covers Planck epoch (a ≈ 6e-32 at t=1e-43 s)
    double log_a_max = std::log(a_target);
    if (log_a_max <= log_a_min) return 0.0;

    double dlog_a = (log_a_max - log_a_min) / static_cast<double>(N);
    double t = 0.0;

    // dt/d(log a) = 1 / H(a)
    double log_a = log_a_min;
    double a_prev = std::exp(log_a);
    double integrand_prev = 1.0 / hubble_from_scale(a_prev);

    for (int i = 1; i <= N; ++i) {
        log_a = log_a_min + i * dlog_a;
        double a_cur = std::exp(log_a);
        double integrand_cur = 1.0 / hubble_from_scale(a_cur);
        // Regra do trapézio
        t += 0.5 * (integrand_prev + integrand_cur) * dlog_a;
        integrand_prev = integrand_cur;
    }
    return t;
}

} // namespace phys
