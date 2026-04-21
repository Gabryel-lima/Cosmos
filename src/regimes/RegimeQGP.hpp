#pragma once
// src/regimes/RegimeQGP.hpp — Regime 1: Plasma Quark-Glúon
// Tempo cósmico: 10⁻³⁵s → 10⁻⁶s | Temperatura: 10¹⁶ keV → 150 keV

#include "IRegime.hpp"
#include <vector>

class RegimeQGP : public IRegime {
public:
    void onEnter(Universe& state) override;
    void onExit() override;
    void update(double cosmic_dt, double scale_factor, double temp_keV,
                Universe& universe) override;
    void render(Renderer& renderer, const Universe& universe) override;
    std::string getName() const override { return "Quark-Gluon Plasma"; }

private:
    void applyYukawaForces(Universe& universe, double temp_keV, double dt);
    void applyCosmicExpansion(Universe& universe, double a_prev, double a_new);
    void hadronize(Universe& universe);   // confinamento quark → hádron

    double prev_scale_factor_ = 0.0;
    bool   hadronized_        = false;
};
