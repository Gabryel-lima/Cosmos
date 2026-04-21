#pragma once
// src/regimes/RegimeStructure.hpp — Regime 4: Formação de Estruturas
// Tempo cósmico: 380.000 anos → 13,8 Gyr | Temperatura: < 3000 K
// Sub-épocas: 4a Idades Escuras, 4b Formação de Galáxias, 4c Evolução Estelar, 4d Formação de Planetas

#include "IRegime.hpp"
#include "../physics/NBody.hpp"
#include <vector>
#include <cstdint>

class RegimeStructure : public IRegime {
public:
    void onEnter(Universe& state) override;
    void onExit() override;
    void update(double cosmic_dt, double scale_factor, double temp_keV,
                Universe& universe) override;
    void render(Renderer& renderer, const Universe& universe) override;
    std::string getName() const override { return "Structure Formation"; }

private:
    void leapfrogKick(Universe& universe, double dt);
    void leapfrogDrift(Universe& universe, double dt);
    void applyCosmicExpansion(Universe& universe, double a_prev, double a_new);
    void checkStarFormation(Universe& universe, double temp_K);
    void updateStellarEvolution(Universe& universe, double cosmic_dt);
    void runFriendsOfFriends(Universe& universe);

    NBodySolver nbody_;
    double prev_scale_factor_ = 0.0;
    int    star_check_frame_  = 0;    // contador para limitar O(N²) de formação estelar

    // Dados de grupo FoF (halos de galáxias)
    struct Halo {
        double cx, cy, cz;    // centro de massa
        double mass;
        int    member_count;
    };
    std::vector<Halo> halos_;
    double last_fof_time_ = 0.0;
};
