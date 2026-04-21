#pragma once
// src/regimes/RegimeInflation.hpp — Regime 0: Inflação / Flutuações Quânticas
// Tempo cósmico: t < 10⁻³⁵ s | Temperatura: T > 10¹⁶ keV
// Fase A: campo escalar 2D φ(x,y); Fase B: extrusão 3D

#include "IRegime.hpp"
#include <vector>

class RegimeInflation : public IRegime {
public:
    void onEnter(Universe& state) override;
    void onExit() override;
    void update(double cosmic_dt, double scale_factor, double temp_keV,
                Universe& universe) override;
    void render(Renderer& renderer, const Universe& universe) override;
    std::string getName() const override { return "Inflation / Quantum Fluctuations"; }

private:
    void initScalarField(Universe& universe);
    void stepScalarField(double cosmic_dt, double H, Universe& universe);
    void extrudeFieldTo3D(Universe& universe);

    double e_folds_ = 0.0;          // contagem de e-folds
    bool   in_phase_b_ = false;     // verdadeiro após ~30 e-folds
    float  extrude_t_  = 0.0f;      // progresso de extrusão 0→1

    static constexpr int   PHI_N  = 256;  // lado da grade
    static constexpr float M2     = 1.0f; // parâmetro de massa² da inflação (slow-roll)
    static constexpr float EFOLDS_3D = 12.0f;
};
