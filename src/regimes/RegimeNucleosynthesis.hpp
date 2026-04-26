#pragma once
// src/regimes/RegimeNucleosynthesis.hpp — Regime 2: Nucleossíntese do Big Bang
// Tempo cósmico: 10⁻⁶s → 20 min | Temperatura: 150 keV → 0,07 keV

#include "IRegime.hpp"

class RegimeNucleosynthesis : public IRegime {
public:
    void onEnter(Universe& state) override;
    void onExit() override;
    void update(double cosmic_dt, double scale_factor, double temp_keV,
                Universe& universe) override;
    void render(Renderer& renderer, const Universe& universe) override;
    std::string getName() const override { return "Big Bang Nucleosynthesis (BBN)"; }

private:
    double total_baryon_density_ = 0.0;
    double prev_scale_factor_ = 0.0;
};
