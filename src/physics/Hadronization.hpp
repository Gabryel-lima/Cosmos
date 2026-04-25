#pragma once

#include "../core/Universe.hpp"

namespace chemistry {

bool isLightNucleus(ParticleType type);
bool isBaryonicParticle(ParticleType type);
int baryonNumber(ParticleType type);
int atomicCharge(ParticleType type);
double restMass(ParticleType type);

struct HadronizationStats {
    size_t formed_protons = 0;
    size_t formed_neutrons = 0;
    size_t confined_gluons = 0;
    size_t residual_quarks = 0;
};

HadronizationStats hadronizeQgp(ParticlePool& pool, double binding_radius = 0.18);
NuclearAbundances inferAbundances(const ParticlePool& pool);

} // namespace chemistry
