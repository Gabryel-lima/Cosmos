// src/physics/ParticlePool.cpp
#include "ParticlePool.hpp"
#include <algorithm>

void ParticlePool::resize(size_t n) {
    x.resize(n); y.resize(n); z.resize(n);
    vx.resize(n); vy.resize(n); vz.resize(n);
    mass.resize(n);
    type.resize(n, ParticleType::GAS);
    charge.resize(n, 0.0f);
    color_r.resize(n, 1.0f); color_g.resize(n, 1.0f); color_b.resize(n, 1.0f);
    luminosity.resize(n, 1.0f);
    temp_particle.resize(n, 0.0f);
    star_state.resize(n, StarState::NONE);
    star_age.resize(n, 0.0);
    flags.resize(n, 0u);
    capacity = n;
}

void ParticlePool::clear() {
    x.clear(); y.clear(); z.clear();
    vx.clear(); vy.clear(); vz.clear();
    mass.clear();
    type.clear(); charge.clear();
    color_r.clear(); color_g.clear(); color_b.clear();
    luminosity.clear(); temp_particle.clear();
    star_state.clear(); star_age.clear();
    flags.clear();
    capacity = 0;
    active   = 0;
}

size_t ParticlePool::add(double px, double py, double pz,
                         double pvx, double pvy, double pvz,
                         double mass_kg, ParticleType t,
                         float cr, float cg, float cb,
                         float charge_val) 
{
    size_t i = x.size();
    x.push_back(px); y.push_back(py); z.push_back(pz);
    vx.push_back(pvx); vy.push_back(pvy); vz.push_back(pvz);
    mass.push_back(mass_kg);
    type.push_back(t);
    charge.push_back(charge_val);
    color_r.push_back(cr); color_g.push_back(cg); color_b.push_back(cb);
    luminosity.push_back(1.0f);
    temp_particle.push_back(0.0f);
    star_state.push_back(StarState::NONE);
    star_age.push_back(0.0);
    flags.push_back(PF_ACTIVE);
    capacity = x.size();
    ++active;
    return i;
}

void ParticlePool::deactivate(size_t i) {
    if (flags[i] & PF_ACTIVE) {
        flags[i] &= ~PF_ACTIVE;
        if (active > 0) --active;
    }
}

void ParticlePool::compact() {
    // Remover todas as partículas desativadas — manter ordenação
    size_t write = 0;
    for (size_t read = 0; read < x.size(); ++read) {
        if (flags[read] & PF_ACTIVE) {
            if (write != read) {
                x[write]            = x[read];
                y[write]            = y[read];
                z[write]            = z[read];
                vx[write]           = vx[read];
                vy[write]           = vy[read];
                vz[write]           = vz[read];
                mass[write]         = mass[read];
                type[write]         = type[read];
                charge[write]       = charge[read];
                color_r[write]      = color_r[read];
                color_g[write]      = color_g[read];
                color_b[write]      = color_b[read];
                luminosity[write]   = luminosity[read];
                temp_particle[write]= temp_particle[read];
                star_state[write]   = star_state[read];
                star_age[write]     = star_age[read];
                flags[write]        = flags[read];
            }
            ++write;
        }
    }
    x.resize(write); y.resize(write); z.resize(write);
    vx.resize(write); vy.resize(write); vz.resize(write);
    mass.resize(write); type.resize(write); charge.resize(write);
    color_r.resize(write); color_g.resize(write); color_b.resize(write);
    luminosity.resize(write); temp_particle.resize(write);
    star_state.resize(write); star_age.resize(write);
    flags.resize(write);
    capacity = write;
    active   = write;
}

void ParticlePool::defaultColor(ParticleType t, float& r, float& g, float& b) {
    switch (t) {
        case ParticleType::QUARK_U:    r=0.10f; g=0.95f; b=0.95f; break;
        case ParticleType::QUARK_D:    r=0.98f; g=0.20f; b=0.78f; break;
        case ParticleType::QUARK_S:    r=0.95f; g=0.92f; b=0.18f; break;
        case ParticleType::ANTIQUARK:  r=1.00f; g=0.58f; b=0.18f; break;
        case ParticleType::GLUON:      r=1.00f; g=0.84f; b=0.28f; break;
        case ParticleType::PROTON:     r=1.00f; g=0.32f; b=0.28f; break;
        case ParticleType::NEUTRON:    r=0.36f; g=0.52f; b=1.00f; break;
        case ParticleType::ELECTRON:   r=0.10f; g=0.88f; b=1.00f; break;
        case ParticleType::PHOTON:     r=1.00f; g=0.97f; b=0.62f; break;
        case ParticleType::DEUTERIUM:  r=0.76f; g=0.20f; b=1.00f; break;
        case ParticleType::HELIUM3:    r=1.00f; g=0.55f; b=0.18f; break;
        case ParticleType::HELIUM4NUCLEI:    r=1.00f; g=0.72f; b=0.08f; break;
        case ParticleType::LITHIUM7:   r=0.28f; g=1.00f; b=0.52f; break;
        case ParticleType::DARK_MATTER:r=0.45f; g=0.18f; b=0.78f; break;
        case ParticleType::STAR:       r=1.00f; g=0.93f; b=0.76f; break;
        case ParticleType::GAS:        r=0.34f; g=0.78f; b=1.00f; break;
        case ParticleType::BLACKHOLE:  r=0.55f; g=0.05f; b=0.05f; break;
        default:                       r=1.0f; g=1.0f; b=1.0f; break;
    }
}
