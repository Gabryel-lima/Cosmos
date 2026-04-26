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
    qcd_color.resize(n, QcdColor::NONE);
    qcd_anticolor.resize(n, QcdColor::NONE);
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
    qcd_color.clear(); qcd_anticolor.clear();
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
    qcd_color.push_back(QcdColor::NONE);
    qcd_anticolor.push_back(QcdColor::NONE);
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
                qcd_color[write]    = qcd_color[read];
                qcd_anticolor[write]= qcd_anticolor[read];
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
    qcd_color.resize(write); qcd_anticolor.resize(write);
    luminosity.resize(write); temp_particle.resize(write);
    star_state.resize(write); star_age.resize(write);
    flags.resize(write);
    capacity = write;
    active   = write;
}

void ParticlePool::defaultColor(ParticleType t, float& r, float& g, float& b) {
    switch (t) {
        case ParticleType::QUARK_U:    r=1.0f;  g=0.0f;  b=0.0f; break;
        case ParticleType::QUARK_D:    r=0.0f;  g=1.0f;  b=0.0f; break;
        case ParticleType::QUARK_S:    r=0.50f; g=0.90f; b=1.0f; break;
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

void ParticlePool::applyQcdTint(ParticleType t, QcdColor color, QcdColor anticolor,
                                float& r, float& g, float& b)
{
    defaultColor(t, r, g, b);
    if (color == QcdColor::NONE && anticolor == QcdColor::NONE) return;

    float pr = r, pg = g, pb = b;
    float cr = pr, cg = pg, cb = pb;
    float ar = pr, ag = pg, ab = pb;
    if (color != QcdColor::NONE) qcd::rgb(color, cr, cg, cb);
    if (anticolor != QcdColor::NONE) qcd::rgb(anticolor, ar, ag, ab);

    if (color != QcdColor::NONE && anticolor != QcdColor::NONE) {
        r = std::clamp(pr * 0.18f + (cr * 0.55f + ar * 0.27f), 0.0f, 1.0f);
        g = std::clamp(pg * 0.18f + (cg * 0.55f + ag * 0.27f), 0.0f, 1.0f);
        b = std::clamp(pb * 0.18f + (cb * 0.55f + ab * 0.27f), 0.0f, 1.0f);
    } else {
        r = std::clamp(pr * 0.20f + cr * 0.80f, 0.0f, 1.0f);
        g = std::clamp(pg * 0.20f + cg * 0.80f, 0.0f, 1.0f);
        b = std::clamp(pb * 0.20f + cb * 0.80f, 0.0f, 1.0f);
    }
}

void ParticlePool::setQcdCharge(size_t i, QcdColor color, QcdColor anticolor) {
    qcd_color[i] = color;
    qcd_anticolor[i] = anticolor;
    applyQcdTint(type[i], color, anticolor, color_r[i], color_g[i], color_b[i]);
}

void ParticlePool::clearQcdCharge(size_t i) {
    qcd_color[i] = QcdColor::NONE;
    qcd_anticolor[i] = QcdColor::NONE;
    defaultColor(type[i], color_r[i], color_g[i], color_b[i]);
}

float ParticlePool::defaultVisualScale(ParticleType t, uint32_t flags) {
    float scale = 1.0f;
    switch (t) {
        case ParticleType::QUARK_U:
        case ParticleType::QUARK_D:
        case ParticleType::QUARK_S:
        case ParticleType::ANTIQUARK:
            scale = 0.55f; break;
        case ParticleType::GLUON:
            scale = 0.65f; break;
        case ParticleType::ELECTRON:
        case ParticleType::NEUTRINO:
            scale = 0.50f; break;
        case ParticleType::PHOTON:
            scale = 0.70f; break;
        case ParticleType::PROTON:
        case ParticleType::NEUTRON:
            scale = 0.95f; break;
        case ParticleType::DEUTERIUM:
            scale = 1.15f; break;
        case ParticleType::HELIUM3:
            scale = 1.28f; break;
        case ParticleType::HELIUM4NUCLEI:
            scale = 1.42f; break;
        case ParticleType::LITHIUM7:
            scale = 1.62f; break;
        case ParticleType::GAS:
            scale = 1.35f; break;
        case ParticleType::DARK_MATTER:
            scale = 1.10f; break;
        case ParticleType::STAR:
            scale = 2.80f; break;
        case ParticleType::BLACKHOLE:
            scale = 2.20f; break;
        default:
            scale = 1.0f; break;
    }

    if ((flags & PF_BOUND) != 0u) {
        scale *= 1.12f;
    }
    if ((flags & PF_STAR_FORMED) != 0u) {
        scale *= 1.20f;
    }
    return scale;
}
