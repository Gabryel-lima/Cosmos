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
        case ParticleType::QUARK_U:    r=0.0f; g=1.0f; b=1.0f; break; // ciano
        case ParticleType::QUARK_D:    r=1.0f; g=0.0f; b=1.0f; break; // magenta
        case ParticleType::QUARK_S:    r=1.0f; g=1.0f; b=0.0f; break; // amarelo
        case ParticleType::ANTIQUARK:  r=1.0f; g=0.5f; b=0.0f; break; // laranja
        case ParticleType::GLUON:      r=1.0f; g=1.0f; b=1.0f; break; // branco
        case ParticleType::PROTON:     r=1.0f; g=0.2f; b=0.2f; break; // vermelho
        case ParticleType::NEUTRON:    r=0.3f; g=0.3f; b=1.0f; break; // azul
        case ParticleType::ELECTRON:   r=0.0f; g=0.8f; b=1.0f; break; // azul claro
        case ParticleType::PHOTON:     r=1.0f; g=1.0f; b=0.8f; break; // branco quente
        case ParticleType::DEUTERIUM:  r=0.7f; g=0.0f; b=1.0f; break; // roxo
        case ParticleType::HELIUM3:    r=1.0f; g=0.4f; b=0.1f; break; // vermelho-laranja
        case ParticleType::HELIUM4:    r=1.0f; g=0.6f; b=0.0f; break; // laranja
        case ParticleType::LITHIUM7:   r=0.0f; g=1.0f; b=0.4f; break; // verde
        case ParticleType::DARK_MATTER:r=0.3f; g=0.0f; b=0.5f; break; // roxo escuro
        case ParticleType::STAR:       r=1.0f; g=0.9f; b=0.7f; break; // amarelo estelar
        case ParticleType::GAS:        r=0.4f; g=0.6f; b=1.0f; break; // gás azul
        case ParticleType::BLACKHOLE:  r=0.0f; g=0.0f; b=0.0f; break; // preto
        default:                       r=1.0f; g=1.0f; b=1.0f; break;
    }
}
