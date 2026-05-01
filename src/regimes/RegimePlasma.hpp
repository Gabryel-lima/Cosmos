#pragma once
// src/regimes/RegimePlasma.hpp — Regime 3: Plasma de Fótons / Recombinação
// Tempo cósmico: 20 min → 380.000 anos | Temperatura: 0,07 keV → 2,6e-4 keV

#include "IRegime.hpp"

class RegimePlasma : public IRegime {
public:
    void onEnter(Universe& state) override;
    void onExit() override;
    void update(double cosmic_dt, double scale_factor, double temp_keV,
                Universe& universe) override;
    void render(Renderer& renderer, const Universe& universe) override;
    std::string getName() const override { return "Photon Plasma / Recombination"; }

private:
    void stepFluid(Universe& universe, double cosmic_dt, double a, double T_keV);
    void solvePoissonGaussSeidel(Universe& universe);
    double computeIonizationFraction(double T_K, double n_b);

    bool   cmb_flash_triggered_ = false;
    float  cmb_flash_t_         = 0.0f;  // progresso da animação de flash 0→1
    double baryon_density_       = 0.0;
    double recombined_fraction_  = 0.0;
    double prev_scale_factor_    = 0.0;
    float  wave_phase_           = 0.0f;
    int    volume_warmup_frames_ = 0;
};
