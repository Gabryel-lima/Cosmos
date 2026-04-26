// src/core/RegimeManager.cpp
#include "RegimeManager.hpp"
#include "CosmicClock.hpp"
#include "SimulationRandom.hpp"
#include "../regimes/RegimeInflation.hpp"
#include "../regimes/RegimeQGP.hpp"
#include "../regimes/RegimeNucleosynthesis.hpp"
#include "../regimes/RegimePlasma.hpp"
#include "../regimes/RegimeStructure.hpp"
#include "../render/Renderer.hpp"
#include "../physics/Friedmann.hpp"
#include "../physics/Constants.hpp"
#include "../physics/Hadronization.hpp"
#include <random>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <array>

namespace {

bool isBaryonicType(ParticleType type) {
    return chemistry::isBaryonicParticle(type);
}

size_t addParticleCopy(ParticlePool& dst, const ParticlePool& src, size_t i,
                       ParticleType type, float jitter = 0.0f,
                       float luminosity = 1.0f, float charge = 0.0f)
{
    static std::mt19937 rng_copy = simrng::makeStream("regime-copy");
    std::uniform_real_distribution<double> offset(-static_cast<double>(jitter),
                                                  static_cast<double>(jitter));
    float cr, cg, cb;
    ParticlePool::defaultColor(type, cr, cg, cb);
    size_t added = dst.add(src.x[i] + offset(rng_copy),
                           src.y[i] + offset(rng_copy),
                           src.z[i] + offset(rng_copy),
                           src.vx[i], src.vy[i], src.vz[i],
                           src.mass[i], type, cr, cg, cb, charge);
    dst.color_r[added] = src.color_r[i];
    dst.color_g[added] = src.color_g[i];
    dst.color_b[added] = src.color_b[i];
    if (type == src.type[i]) {
        dst.qcd_color[added] = src.qcd_color[i];
        dst.qcd_anticolor[added] = src.qcd_anticolor[i];
    } else {
        dst.clearQcdCharge(added);
    }
    dst.luminosity[added] = luminosity;
    dst.star_state[added] = src.star_state[i];
    dst.star_age[added] = src.star_age[i];
    dst.flags[added] = (type == src.type[i]) ? src.flags[i] : PF_ACTIVE;
    return added;
}

std::mt19937& initRng() {
    static std::mt19937 rng = simrng::makeStream("regime-init");
    return rng;
}

QcdColor randomQcdPrimary(std::mt19937& rng) {
    switch (rng() % 3u) {
        case 0: return QcdColor::RED;
        case 1: return QcdColor::GREEN;
        default:return QcdColor::BLUE;
    }
}

void randomDirectionalGluon(std::mt19937& rng, QcdColor& color, QcdColor& anticolor) {
    switch (rng() % 6u) {
        case 0: color = QcdColor::RED;   anticolor = QcdColor::ANTI_GREEN; break;
        case 1: color = QcdColor::RED;   anticolor = QcdColor::ANTI_BLUE;  break;
        case 2: color = QcdColor::GREEN; anticolor = QcdColor::ANTI_RED;   break;
        case 3: color = QcdColor::GREEN; anticolor = QcdColor::ANTI_BLUE;  break;
        case 4: color = QcdColor::BLUE;  anticolor = QcdColor::ANTI_RED;   break;
        default:color = QcdColor::BLUE;  anticolor = QcdColor::ANTI_GREEN; break;
    }
}

double qgpRestMass(ParticleType type) {
    constexpr double inv_c2 = 1.0 / (phys::c * phys::c);
    switch (type) {
        case ParticleType::QUARK_U: return 2.2 * phys::MeV * inv_c2;
        case ParticleType::QUARK_D: return 4.7 * phys::MeV * inv_c2;
        case ParticleType::QUARK_S: return 93.0 * phys::MeV * inv_c2;
        case ParticleType::GLUON:   return 0.0;
        default:                    return phys::m_p / 3.0;
    }
}

size_t activeParticleCount(const ParticlePool& pool) {
    size_t active = 0;
    for (uint32_t flags : pool.flags) {
        if (flags & PF_ACTIVE) ++active;
    }
    return active;
}

ParticlePool resampleParticlePoolLOD(const ParticlePool& src, size_t target_size) {
    ParticlePool sampled;
    const size_t active = activeParticleCount(src);
    if (active == 0 || target_size == 0 || target_size == active) return src;

    constexpr size_t kTypeCount = static_cast<size_t>(ParticleType::COUNT);
    std::array<std::vector<size_t>, kTypeCount> buckets;
    for (size_t i = 0; i < src.x.size(); ++i) {
        if (!(src.flags[i] & PF_ACTIVE)) continue;
        buckets[static_cast<size_t>(src.type[i])].push_back(i);
    }

    std::array<size_t, kTypeCount> quotas{};
    std::array<double, kTypeCount> remainders{};
    size_t allocated = 0;
    for (size_t type = 0; type < kTypeCount; ++type) {
        if (buckets[type].empty()) continue;
        const double exact = static_cast<double>(target_size) * static_cast<double>(buckets[type].size()) / static_cast<double>(active);
        quotas[type] = static_cast<size_t>(std::floor(exact));
        remainders[type] = exact - static_cast<double>(quotas[type]);
        allocated += quotas[type];
    }

    while (allocated < target_size) {
        size_t best_type = kTypeCount;
        double best_remainder = -1.0;
        for (size_t type = 0; type < kTypeCount; ++type) {
            if (buckets[type].empty()) continue;
            if (remainders[type] > best_remainder) {
                best_remainder = remainders[type];
                best_type = type;
            }
        }
        if (best_type == kTypeCount) break;
        ++quotas[best_type];
        remainders[best_type] = 0.0;
        ++allocated;
    }

    for (size_t type = 0; type < kTypeCount; ++type) {
        const auto& bucket = buckets[type];
        if (bucket.empty() || quotas[type] == 0) continue;

        if (quotas[type] <= bucket.size()) {
            for (size_t pick = 0; pick < quotas[type]; ++pick) {
                const size_t pos = std::min(bucket.size() - 1,
                    static_cast<size_t>(((static_cast<double>(pick) + 0.5) * bucket.size()) / quotas[type]));
                const size_t idx = bucket[pos];
                addParticleCopy(sampled, src, idx, src.type[idx], 0.0f, src.luminosity[idx], src.charge[idx]);
            }
            continue;
        }

        for (size_t idx : bucket) {
            addParticleCopy(sampled, src, idx, src.type[idx], 0.0f, src.luminosity[idx], src.charge[idx]);
        }
        const size_t extra = quotas[type] - bucket.size();
        for (size_t pick = 0; pick < extra; ++pick) {
            const size_t idx = bucket[pick % bucket.size()];
            addParticleCopy(sampled, src, idx, src.type[idx], 0.005f, src.luminosity[idx], src.charge[idx]);
        }
    }

    sampled.active = activeParticleCount(sampled);
    sampled.capacity = sampled.x.size();
    return sampled;
}

double targetTemperatureForRegime(int regime_index) {
    switch (regime_index) {
        case 0: return CosmicClock::T_INFLATION_END * 10.0;
        case 1: return std::sqrt(CosmicClock::T_INFLATION_END * CosmicClock::T_QGP_END);
        case 2: return std::sqrt(CosmicClock::T_QGP_END * CosmicClock::T_BBN_END);
        case 3: return std::sqrt(CosmicClock::T_BBN_END * CosmicClock::T_RECOMBINATION);
        case 4: return std::sqrt(CosmicClock::T_RECOMBINATION * CosmicClock::T_DARK_AGES);
        case 5: return std::sqrt(CosmicClock::T_DARK_AGES * CosmicClock::T_REIONIZATION);
        case 6: return CosmicClock::T_REIONIZATION * 0.15;
        default: return CosmicClock::T_INFLATION_END;
    }
}

void copyParticleState(ParticlePool& pool, size_t idx, ParticleType type,
                       double mass, float luminosity, float charge,
                       StarState star_state = StarState::NONE)
{
    pool.type[idx] = type;
    pool.mass[idx] = mass;
    pool.charge[idx] = charge;
    ParticlePool::defaultColor(type, pool.color_r[idx], pool.color_g[idx], pool.color_b[idx]);
    pool.clearQcdCharge(idx);
    pool.luminosity[idx] = luminosity;
    pool.star_state[idx] = star_state;
    pool.star_age[idx] = 0.0;
    pool.flags[idx] |= PF_ACTIVE;
}

void imprintStructureTemplate(ParticlePool& dst, const ParticlePool& inherited, int to) {
    std::vector<size_t> gas_slots;
    for (size_t i = 0; i < dst.x.size(); ++i) {
        if (!(dst.flags[i] & PF_ACTIVE)) continue;
        if (dst.type[i] == ParticleType::GAS) gas_slots.push_back(i);
    }
    if (gas_slots.empty()) return;

    std::vector<size_t> inherited_stars;
    std::vector<size_t> inherited_blackholes;
    for (size_t i = 0; i < inherited.x.size(); ++i) {
        if (!(inherited.flags[i] & PF_ACTIVE)) continue;
        if (inherited.type[i] == ParticleType::STAR) inherited_stars.push_back(i);
        else if (inherited.type[i] == ParticleType::BLACKHOLE) inherited_blackholes.push_back(i);
    }

    auto mapSlots = [&](const std::vector<size_t>& src_indices, ParticleType target_type) {
        if (src_indices.empty() || gas_slots.empty()) return;
        const size_t limit = std::min(src_indices.size(), gas_slots.size());
        for (size_t pick = 0; pick < limit; ++pick) {
            const size_t dst_pos = gas_slots[(pick * gas_slots.size()) / limit];
            const size_t src_idx = src_indices[pick];
            copyParticleState(dst, dst_pos, target_type,
                              (target_type == ParticleType::BLACKHOLE) ? RegimeConfig::MASS_BLACKHOLE : std::max(inherited.mass[src_idx], RegimeConfig::MASS_STAR),
                              inherited.luminosity[src_idx], inherited.charge[src_idx], inherited.star_state[src_idx]);
        }
    };

    mapSlots(inherited_stars, ParticleType::STAR);
    mapSlots(inherited_blackholes, ParticleType::BLACKHOLE);

    for (size_t idx : gas_slots) {
        if (dst.type[idx] != ParticleType::GAS) continue;
        if (to == 4) {
            dst.luminosity[idx] = 0.18f;
            dst.color_r[idx] = 0.24f; dst.color_g[idx] = 0.55f; dst.color_b[idx] = 0.95f;
        } else if (to == 5) {
            dst.luminosity[idx] = 0.35f;
            dst.color_r[idx] = 0.34f; dst.color_g[idx] = 0.78f; dst.color_b[idx] = 1.0f;
        }
    }
}

ParticlePool remapParticlesForRegime(int to, const Universe& previous) {
    ParticlePool next;
    ParticlePool source = previous.particles;
    if (to == 2) {
        chemistry::hadronizeQgp(source);
    }
    const ParticlePool& src = source;
    if (src.x.empty()) return next;

    std::vector<size_t> plasma_baryons;

    for (size_t i = 0; i < src.x.size(); ++i) {
        if (!(src.flags[i] & PF_ACTIVE)) continue;

        switch (to) {
            case 2: {
                if (!chemistry::isLightNucleus(src.type[i])) continue;
                addParticleCopy(next, src, i, src.type[i], 0.0f, src.luminosity[i], src.charge[i]);
                break;
            }

            case 3: {
                if (!chemistry::isLightNucleus(src.type[i])) continue;
                ParticleType nucleus = src.type[i];
                plasma_baryons.push_back(i);
                addParticleCopy(next, src, i, nucleus, 0.0025f, 1.2f,
                                static_cast<float>(chemistry::atomicCharge(nucleus)));

                for (int electron = 0; electron < chemistry::atomicCharge(nucleus); ++electron) {
                    addParticleCopy(next, src, i, ParticleType::ELECTRON,
                                    0.008f + static_cast<float>(electron) * 0.004f,
                                    0.9f, -1.0f);
                }
                break;
            }

            case 4:
            case 5:
            case 6: {
                ParticleType mapped = src.type[i];
                if (mapped == ParticleType::PHOTON || mapped == ParticleType::NEUTRINO) continue;
                if (mapped == ParticleType::ELECTRON || isBaryonicType(mapped)) {
                    if (mapped != ParticleType::STAR && mapped != ParticleType::BLACKHOLE) {
                        mapped = ParticleType::GAS;
                    }
                }
                size_t added = addParticleCopy(next, src, i, mapped);
                if (mapped == ParticleType::GAS || mapped == ParticleType::DARK_MATTER) {
                    next.mass[added] = (mapped == ParticleType::DARK_MATTER) ? RegimeConfig::MASS_DARK_MATTER : RegimeConfig::MASS_GAS;
                    if (mapped == ParticleType::GAS) {
                        next.charge[added] = 0.0f;
                        next.luminosity[added] = (to == 4) ? 0.18f : (to == 5 ? 0.35f : 0.8f);
                    }
                }

                size_t star_step = (to == 5) ? RegimeConfig::TRANS_STRUCT_STAR_SPAWN_STEP * 2
                                             : RegimeConfig::TRANS_STRUCT_STAR_SPAWN_STEP;
                if (to >= 5 && mapped == ParticleType::GAS && i % std::max<size_t>(star_step, 1) == 0) {
                    size_t star = next.add(src.x[i], src.y[i], src.z[i],
                                           src.vx[i], src.vy[i], src.vz[i],
                                           RegimeConfig::MASS_STAR, ParticleType::STAR,
                                           (to == 5) ? 0.82f : 1.0f,
                                           (to == 5) ? 0.9f  : 0.92f,
                                           (to == 5) ? 1.0f  : 0.72f,
                                           0.0f);
                    next.star_state[star] = (to == 5) ? StarState::MAIN_SEQUENCE : StarState::PROTOSTAR;
                    next.luminosity[star] = (to == 5) ? 4.8f : 4.0f;
                    next.flags[star] |= PF_STAR_FORMED;
                }
                if (to == 6 && mapped == ParticleType::GAS && i % (RegimeConfig::TRANS_STRUCT_STAR_SPAWN_STEP * 12) == 0) {
                    size_t bh = next.add(src.x[i], src.y[i], src.z[i],
                                         src.vx[i], src.vy[i], src.vz[i],
                                         RegimeConfig::MASS_BLACKHOLE, ParticleType::BLACKHOLE,
                                         0.55f, 0.05f, 0.05f, 0.0f);
                    next.star_state[bh] = StarState::BLACK_HOLE;
                    next.luminosity[bh] = 5.5f;
                }
                break;
            }

            default:
                break;
        }
    }

    if (to == 3 && !plasma_baryons.empty()) {
        const size_t photon_target = std::max<size_t>(1,
            static_cast<size_t>(std::lround(plasma_baryons.size() *
                (static_cast<double>(RegimeConfig::PLASMA_PHOTON_COUNT) /
                 std::max(1, RegimeConfig::PLASMA_BARYON_COUNT)))));
        for (size_t photon = 0; photon < photon_target; ++photon) {
            const size_t src_idx = plasma_baryons[photon % plasma_baryons.size()];
            addParticleCopy(next, src, src_idx, ParticleType::PHOTON, 0.03f, 1.6f, 0.0f);
        }
    }

    return next;
}

void inheritStateAcrossTransition(int from, int to, const Universe& previous, InitialState& next) {
    if (from == to) return;

    // Ao ir para os regimes pós-plasma, o escopo da simulação dá um pulo
    // de uma pequena escala local de plasma para uma caixa cosmológica de 50 Megaparsecs.
    // Manter as posições literais das partículas do plasma (espremidas em [-2.5, 2.5]) 
    // num vazio de 50 Mpc arruinaria a simulação da teia cósmica.
    // Portanto, para eras >= 4, preservamos a malha cosmológica do buildInitialState
    // e apenas injetamos a composição herdada nas fases baryônicas.
    ParticlePool remapped = remapParticlesForRegime(to, previous);
    if (!remapped.x.empty()) {
        size_t target_size = next.particles.x.size();

        if (to < 4 && target_size > 0 && target_size != activeParticleCount(remapped)) {
            remapped = resampleParticlePoolLOD(remapped, target_size);
        }

        if (to == 2 || to == 3) {
            next.abundances = chemistry::inferAbundances(remapped);
        }

        if (to >= 4) {
            imprintStructureTemplate(next.particles, remapped, to);
        } else if (activeParticleCount(remapped) > 0) {
            next.particles = std::move(remapped);
        }
    }

    if (to >= 3 && previous.density_field.NX > 0 && previous.density_field.data.size() == next.field.data.size()) {
        next.field = previous.density_field;
    }
}

} // namespace

// ── Construção ─────────────────────────────────────────────────────────────

RegimeManager::RegimeManager() {
    regimes_[0] = std::make_unique<RegimeInflation>();
    regimes_[1] = std::make_unique<RegimeQGP>();
    regimes_[2] = std::make_unique<RegimeNucleosynthesis>();
    regimes_[3] = std::make_unique<RegimePlasma>();
    regimes_[4] = std::make_unique<RegimeStructure>(StructurePhase::DARK_AGES);
    regimes_[5] = std::make_unique<RegimeStructure>(StructurePhase::REIONIZATION);
    regimes_[6] = std::make_unique<RegimeStructure>(StructurePhase::MATURE);
}

// ── Construtores de Estado Inicial ─────────────────────────────────────────────────────

/// Aproximação de Zel'dovich: desloca grade regular com perturbações de densidade.
/// Produz distribuição inicial de partículas cosmologicamente motivada em z~20.
static void zelDovichDisplace(ParticlePool& pool, int N_cbrt, double box_size) {
    std::normal_distribution<double> gauss(0.0, 0.05 * box_size);
    std::normal_distribution<double> velocity(0.0, 0.015);
    std::mt19937& rng_init = initRng();
    pool.clear();
    double spacing = box_size / static_cast<double>(N_cbrt);
    double half = box_size * 0.5;  // centre particles around origin
    double R2 = half * half; // reject points outside sphere to prevent cuboid collapse artifacts
    for (int k = 0; k < N_cbrt; ++k)
    for (int j = 0; j < N_cbrt; ++j)
    for (int i = 0; i < N_cbrt; ++i) {
        double px = (i + 0.5) * spacing - half;
        double py = (j + 0.5) * spacing - half;
        double pz = (k + 0.5) * spacing - half;
        if (px * px + py * py + pz * pz > R2) continue; // Shape as a sphere
        
        double x = px + gauss(rng_init);
        double y = py + gauss(rng_init);
        double z = pz + gauss(rng_init);
        float cr, cg, cb;
        // 80% matéria escura, 20% gás
        ParticleType t = (rng_init() % RegimeConfig::STRUCT_GAS_RATIO_DIVISOR == 0) ? ParticleType::GAS : ParticleType::DARK_MATTER;
        double mass = (t == ParticleType::DARK_MATTER) ? RegimeConfig::MASS_DARK_MATTER : RegimeConfig::MASS_GAS;
        ParticlePool::defaultColor(t, cr, cg, cb);
        size_t added = pool.add(x, y, z,
                 velocity(rng_init), velocity(rng_init), velocity(rng_init),
                 mass, t, cr, cg, cb);
        pool.luminosity[added] = (t == ParticleType::GAS) ? 0.7f : 0.05f; // Gás visível, Matéria Escura bem discreta
    }
}

// ── Helper para geração de posições esféricas ────────────────────────────────
static void randomPosInSphere(double r_max, double& px, double& py, double& pz) {
    std::uniform_real_distribution<double> dist(-r_max, r_max);
    std::mt19937& rng_init = initRng();
    do {
        px = dist(rng_init);
        py = dist(rng_init);
        pz = dist(rng_init);
    } while (px*px + py*py + pz*pz > r_max*r_max);
}

static void randomPosInSphereWithClearance(double r_max, double min_distance,
                                           const ParticlePool& pool,
                                           double& px, double& py, double& pz)
{
    double min_distance2 = min_distance * min_distance;
    for (int attempt = 0; attempt < 96; ++attempt) {
        randomPosInSphere(r_max, px, py, pz);
        bool overlaps = false;
        for (size_t i = 0; i < pool.x.size(); ++i) {
            if (!(pool.flags[i] & PF_ACTIVE)) continue;
            double dx = pool.x[i] - px;
            double dy = pool.y[i] - py;
            double dz = pool.z[i] - pz;
            if (dx * dx + dy * dy + dz * dz < min_distance2) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) return;
    }

    randomPosInSphere(r_max, px, py, pz);
}

static void randomUnitDirection(double& dx, double& dy, double& dz) {
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::mt19937& rng_init = initRng();
    do {
        dx = dist(rng_init);
        dy = dist(rng_init);
        dz = dist(rng_init);
    } while (dx * dx + dy * dy + dz * dz < 1e-6);
    double inv_len = 1.0 / std::sqrt(dx * dx + dy * dy + dz * dz);
    dx *= inv_len;
    dy *= inv_len;
    dz *= inv_len;
}

InitialState RegimeManager::buildInitialState(int regime_index) {
    InitialState st;
    std::mt19937& rng_init = initRng();
    int idx = std::clamp(regime_index, 0, CosmicClock::LAST_REGIME_INDEX);
    st.scale_factor    = phys::scale_at_temperature_keV(targetTemperatureForRegime(idx));
    if (st.scale_factor <= 0.0 || !std::isfinite(st.scale_factor))
        st.scale_factor = 1e-28;
    st.cosmic_time     = CosmicClock::REGIME_START_TIMES[static_cast<size_t>(idx)];
    st.temperature_keV = phys::temperature_keV_from_scale(st.scale_factor);

    // Sugestões de câmera
    CameraState cam;
    switch (idx) {
        case 0: cam.ortho = true;  cam.zoom = 1.0; break;
        case 1: cam.ortho = false; cam.zoom = 3.0; break;
        case 2: cam.ortho = false; cam.zoom = 3.0; break;
        case 3: cam.ortho = false; cam.zoom = 7.5; break;
        case 4: cam.ortho = false; cam.zoom = 38.0; break;
        case 5: cam.ortho = false; cam.zoom = 45.0; break;
        case 6: cam.ortho = false; cam.zoom = 52.0; break;
    }
    cam.pos_z = static_cast<double>(cam.zoom) * 1.5;
    st.suggested_camera = cam;

    // Preencher partículas ────────────────────────────────────────────────────
    switch (idx) {
        case 0:
            // Inflação: sem partículas; campo escalar inicializado por RegimeInflation::onEnter
            break;

        case 1: {
            // PQG: 2000 quarks – pequeno o suficiente para Yukawa O(N²) rodar em tempo real
            int N = RegimeConfig::QGP_QUARK_COUNT;
            std::normal_distribution<double> vel_dist(0.0, 0.05);
            static const ParticleType quark_types[3] = {
                ParticleType::QUARK_U, ParticleType::QUARK_D, ParticleType::QUARK_S
            };
            for (int i = 0; i < N; ++i) {
                double px, py, pz;
                randomPosInSphereWithClearance(0.5, RegimeConfig::QGP_INIT_MIN_SEPARATION,
                                               st.particles, px, py, pz);
                double vx = vel_dist(rng_init), vy = vel_dist(rng_init), vz = vel_dist(rng_init);
                ParticleType t = quark_types[rng_init() % 3];
                float cr, cg, cb; ParticlePool::defaultColor(t, cr, cg, cb);
                size_t added = st.particles.add(px, py, pz, vx, vy, vz, qgpRestMass(t), t, cr, cg, cb,
                                                (t == ParticleType::QUARK_U) ? 2.0f/3.0f : -1.0f/3.0f);
                st.particles.setQcdCharge(added, randomQcdPrimary(rng_init));
                st.particles.luminosity[added] = 2.2f;
            }
            // Glúons mediadores
            for (int i = 0; i < N / RegimeConfig::QGP_GLUON_RATIO_DIVISOR; ++i) {
                double px, py, pz;
                randomPosInSphereWithClearance(0.5, RegimeConfig::QGP_INIT_MIN_SEPARATION * 0.75,
                                               st.particles, px, py, pz);
                size_t added = st.particles.add(px, py, pz,
                                                vel_dist(rng_init), vel_dist(rng_init), vel_dist(rng_init),
                                                qgpRestMass(ParticleType::GLUON), ParticleType::GLUON, 1.0f, 0.8f, 0.2f, 0.0f);
                QcdColor color = QcdColor::NONE;
                QcdColor anticolor = QcdColor::NONE;
                randomDirectionalGluon(rng_init, color, anticolor);
                st.particles.setQcdCharge(added, color, anticolor);
                st.particles.luminosity[added] = 2.8f;
            }
            break;
        }

        case 2: {
            // NBB: prótons e nêutrons (da hadronização)
            st.abundances = NuclearAbundances{};
            st.abundances.Xp = RegimeConfig::BBN_INIT_XP; 
            st.abundances.Xn = RegimeConfig::BBN_INIT_XN;
            int N = RegimeConfig::BBN_NUCLEON_COUNT;
            std::normal_distribution<double> vel_dist(0.0, 0.01);
            for (int i = 0; i < N; ++i) {
                double px, py, pz;
                randomPosInSphereWithClearance(0.5, RegimeConfig::BBN_INIT_MIN_SEPARATION,
                                               st.particles, px, py, pz);
                bool is_proton = (rng_init() % RegimeConfig::BBN_PROTON_RATIO != 0);
                ParticleType t = is_proton ? ParticleType::PROTON : ParticleType::NEUTRON;
                float cr, cg, cb; ParticlePool::defaultColor(t, cr, cg, cb);
                size_t added = st.particles.add(px, py, pz,
                                                vel_dist(rng_init), vel_dist(rng_init), vel_dist(rng_init),
                                                phys::m_p, t, cr, cg, cb);
                st.particles.luminosity[added] = is_proton ? 1.7f : 1.5f;
            }
            break;
        }

        case 3: {
            // Plasma: inicializar grade de fluido com amplitude visível de perturbações
            int N = RegimeConfig::PLASMA_GRID_SIZE;
            st.field.resize(N, N, N);
            // Perturbações BAO: amplitude 0.1 para contraste visível no renderizador de volume
            std::normal_distribution<float> delta_noise(0.0f, 0.1f);
            for (float& v : st.field.data) v = delta_noise(rng_init);

            std::normal_distribution<double> vel_dist(0.0, 0.06);
            for (int i = 0; i < RegimeConfig::PLASMA_BARYON_COUNT; ++i) {
                double px, py, pz;
                randomPosInSphereWithClearance(2.5, RegimeConfig::PLASMA_INIT_BARYON_MIN_SEPARATION,
                                               st.particles, px, py, pz);
                bool is_helium = (i % RegimeConfig::PLASMA_HELIUM_RATIO_DIVISOR == 0);
                ParticleType baryon = is_helium ? ParticleType::HELIUM4NUCLEI : ParticleType::PROTON;
                float br, bg, bb;
                ParticlePool::defaultColor(baryon, br, bg, bb);
                st.particles.add(px, py, pz,
                                 vel_dist(rng_init), vel_dist(rng_init), vel_dist(rng_init),
                                 is_helium ? phys::m_p * 4.0 : phys::m_p,
                                 baryon, br, bg, bb,
                                 is_helium ? 2.0f : 1.0f);

                double ox, oy, oz;
                randomUnitDirection(ox, oy, oz);
                constexpr double electron_offset = 0.035;
                float er, eg, eb;
                ParticlePool::defaultColor(ParticleType::ELECTRON, er, eg, eb);
                st.particles.add(px + ox * electron_offset,
                                 py + oy * electron_offset,
                                 pz + oz * electron_offset,
                                 vel_dist(rng_init) - ox * 0.02,
                                 vel_dist(rng_init) - oy * 0.02,
                                 vel_dist(rng_init) - oz * 0.02,
                                 phys::m_e, ParticleType::ELECTRON, er, eg, eb, -1.0f);
            }
            for (int i = 0; i < RegimeConfig::PLASMA_PHOTON_COUNT; ++i) {
                double px, py, pz;
                randomPosInSphere(2.5, px, py, pz);
                float pr, pg, pb;
                ParticlePool::defaultColor(ParticleType::PHOTON, pr, pg, pb);
                size_t photon = st.particles.add(px, py, pz,
                                                 vel_dist(rng_init) * 2.0, vel_dist(rng_init) * 2.0, vel_dist(rng_init) * 2.0,
                                                 0.0, ParticleType::PHOTON, pr, pg, pb, 0.0f);
                st.particles.luminosity[photon] = 2.5f;
            }
            break;
        }

        case 4:
        case 5:
        case 6: {
            // Estruturas tardias: grade de Zel'dovich em z~20 — N_cbrt³ partículas
            int N_cbrt = RegimeConfig::STRUCT_ZELDOVICH_N_CBRT;  // ~15 625 partículas (gerenciável para formação estelar)
            double box = RegimeConfig::STRUCT_BOX_SIZE_MPC;  // Mpc comóvel (câmera vê melhor)
            zelDovichDisplace(st.particles, N_cbrt, box);
            // Campo de densidade semente
            st.field.resize(RegimeConfig::STRUCT_GRID_SIZE, RegimeConfig::STRUCT_GRID_SIZE, RegimeConfig::STRUCT_GRID_SIZE);
            std::normal_distribution<float> dn(0.0f, 0.05f);
            for (float& v : st.field.data) v = dn(rng_init);

            for (size_t i = 0; i < st.particles.x.size(); ++i) {
                if (st.particles.type[i] == ParticleType::GAS) {
                    st.particles.luminosity[i] = (idx == 4) ? 0.18f : (idx == 5 ? 0.35f : 0.7f);
                    if (idx == 4) {
                        st.particles.color_r[i] = 0.24f; st.particles.color_g[i] = 0.55f; st.particles.color_b[i] = 0.95f;
                    } else if (idx == 5) {
                        st.particles.color_r[i] = 0.34f; st.particles.color_g[i] = 0.78f; st.particles.color_b[i] = 1.0f;
                    }
                } else if (st.particles.type[i] == ParticleType::DARK_MATTER) {
                    st.particles.color_r[i] = 0.45f; st.particles.color_g[i] = 0.18f; st.particles.color_b[i] = 0.78f;
                    st.particles.luminosity[i] = 0.05f;
                }
            }

            if (idx >= 5) {
                const size_t star_step = (idx == 5) ? RegimeConfig::STRUCT_STAR_SPAWN_STEP * 2
                                                    : RegimeConfig::STRUCT_STAR_SPAWN_STEP;
                for (size_t i = 0; i < st.particles.x.size(); i += star_step) {
                    if (st.particles.type[i] != ParticleType::GAS) continue;
                    size_t star = st.particles.add(st.particles.x[i], st.particles.y[i], st.particles.z[i],
                                                   st.particles.vx[i], st.particles.vy[i], st.particles.vz[i],
                                                   RegimeConfig::MASS_STAR, ParticleType::STAR,
                                                   (idx == 5) ? 0.82f : 1.0f,
                                                   (idx == 5) ? 0.9f  : 0.92f,
                                                   (idx == 5) ? 1.0f  : 0.72f,
                                                   0.0f);
                    st.particles.star_state[star] = (idx == 5) ? StarState::MAIN_SEQUENCE : StarState::PROTOSTAR;
                    st.particles.luminosity[star] = (idx == 5) ? 4.8f : 4.0f;
                    st.particles.flags[star] |= PF_STAR_FORMED;
                }
            }

            if (idx == 6) {
                for (size_t i = RegimeConfig::STRUCT_BH_SPAWN_OFFSET; i < st.particles.x.size(); i += RegimeConfig::STRUCT_BH_SPAWN_STEP) {
                    size_t bh = st.particles.add(st.particles.x[i], st.particles.y[i], st.particles.z[i],
                                                 0.0, 0.0, 0.0,
                                                 RegimeConfig::MASS_BLACKHOLE, ParticleType::BLACKHOLE,
                                                 0.55f, 0.05f, 0.05f, 0.0f);
                    st.particles.luminosity[bh] = 6.0f;
                    st.particles.star_state[bh] = StarState::BLACK_HOLE;
                }
            }
            break;
        }
    }
    return st;
}

// ── Aplicar estado inicial ao Universo ──────────────────────────────────────────────

void RegimeManager::applyInitialState(int regime_index, InitialState& state,
                                       Universe& universe)
{
    universe.particles      = std::move(state.particles);
    universe.abundances     = state.abundances;
    
    // AVISO: Não sobrescrever scale_factor, temperature_keV ou cosmic_time aqui!
    // O relógio (CosmicClock) é a única fonte da verdade e as evoluções contínuas
    // (automáticas) não podem sofrer descontinuidades analíticas da InitialState.
    // main.cpp sincroniza esses valores do relógio para o universo todo quadro.
    
    universe.regime_index   = regime_index;
    universe.active_particles = static_cast<int>(std::count_if(
        universe.particles.flags.begin(), universe.particles.flags.end(),
        [](uint32_t f){ return f & PF_ACTIVE; }));

    if (regime_index >= 3) {
        universe.density_field = std::move(state.field);
        if (universe.density_field.NX > 0) {
            int N = universe.density_field.NX;
            universe.velocity_x.resize(N, N, N);
            universe.velocity_y.resize(N, N, N);
            universe.velocity_z.resize(N, N, N);
        }
    }
}

// ── Tick: verificar e acionar transições automáticas ─────────────────────────────

void RegimeManager::tick(CosmicClock& clock, Universe& universe, double real_dt_seconds) {
    float frame_dt = static_cast<float>(std::max(0.0, real_dt_seconds));

    if (in_transition_) {
        // Avançar o blend com o delta real do quadro para estabilizar a duração visual.
        transition_elapsed_ += frame_dt;
        transition_t_ = std::clamp(transition_elapsed_ / transition_dur_, 0.0f, 1.0f);

        if (transition_t_ >= 1.0f) {
            in_transition_ = false;
            active_index_  = transition_to_;
            regime_elapsed_real_ = 0.0f;
            transition_from_universe_ = Universe{};
            std::printf("[REGIME] Transition %d→%d complete.\n",
                        transition_from_, transition_to_);
        }
        universe.active_particles = static_cast<int>(
            std::count_if(universe.particles.flags.begin(),
                          universe.particles.flags.end(),
                          [](uint32_t f){ return f & PF_ACTIVE; }));
        return;
    }

    // Atualizar física do regime atual
    if (regimes_[active_index_]) {
// Atualização de física é chamada pelo loop principal (após clock.step),
        // não por tick(). tick() apenas trata verificações de transição.
        (void)universe;
    }

    regime_elapsed_real_ += frame_dt;

    checkAndTransition(clock, universe);

    // Sincronizar estado do universo
    universe.active_particles = static_cast<int>(
        std::count_if(universe.particles.flags.begin(),
                      universe.particles.flags.end(),
                      [](uint32_t f){ return f & PF_ACTIVE; }));
}

void RegimeManager::checkAndTransition(CosmicClock& clock, Universe& universe) {
    float required_dwell = min_regime_dwell_s_[static_cast<size_t>(std::clamp(active_index_, 0, CosmicClock::LAST_REGIME_INDEX))];
    if (regime_elapsed_real_ < required_dwell) {
        return;
    }

    int observed_regime = clock.getCurrentRegimeIndex();
    if (observed_regime > active_index_ && !in_transition_) {
        // Avançar apenas um regime por vez para impedir saltos visuais.
        int next_regime = std::min(active_index_ + 1, CosmicClock::LAST_REGIME_INDEX);
        beginTransition(active_index_, next_regime, universe, clock);
    }
}

void RegimeManager::beginTransition(int from, int to, Universe& universe,
                                     CosmicClock& clock)
{
    std::printf("[REGIME] Transitioning %d→%d at T=%.4e keV, t=%.4e s\n",
                from, to, clock.getTemperatureKeV(), clock.getCosmicTime());

    // Aplicar escala do regime destino imediatamente — física precisa de cosmic_dt correto
    // desde o primeiro quadro para que as animações do novo regime carreguem.
    clock.rebaseTimeScaleForRegime(to);

    // Sair do regime atual
    if (regimes_[from]) regimes_[from]->onExit();
    transition_from_universe_ = universe;
    transition_from_universe_.regime_index = from;

    // Construir estado inicial para o novo regime
    InitialState st = buildInitialState(to);
    inheritStateAcrossTransition(from, to, universe, st);
    applyInitialState(to, st, universe);
    std::printf("[REGIME] Entering regime %d with active_particles=%d\n",
                to,
                static_cast<int>(std::count_if(universe.particles.flags.begin(),
                                               universe.particles.flags.end(),
                                               [](uint32_t f){ return f & PF_ACTIVE; })));

    // Entrar no novo regime
    if (regimes_[to]) regimes_[to]->onEnter(universe);

    in_transition_      = true;
    transition_from_    = from;
    transition_to_      = to;
    transition_t_       = 0.0f;
    transition_elapsed_ = 0.0f;
    speed_mult_at_start_ = clock.getSpeedMultiplier(); // preservar multiplicação do usuário
    active_index_       = to;  // física executa no novo regime imediatamente
}

// ── Navegação ───────────────────────────────────────────────────────────────

void RegimeManager::jumpToRegime(int index, CosmicClock& clock, Universe& universe) {
    int idx = std::clamp(index, 0, CosmicClock::LAST_REGIME_INDEX);
    if (regimes_[active_index_]) regimes_[active_index_]->onExit();

    clock.jumpToRegime(idx);

    InitialState st = buildInitialState(idx);
    inheritStateAcrossTransition(active_index_, idx, universe, st);
    applyInitialState(idx, st, universe);
    active_index_ = idx;
    in_transition_ = false;
    regime_elapsed_real_ = 0.0f;
    transition_from_universe_ = Universe{};

    if (regimes_[idx]) regimes_[idx]->onEnter(universe);

    std::printf("[REGIME] Jumped to regime %d with active_particles=%d.\n",
                idx,
                static_cast<int>(std::count_if(universe.particles.flags.begin(),
                                               universe.particles.flags.end(),
                                               [](uint32_t f){ return f & PF_ACTIVE; })));
}

// ── Renderização ───────────────────────────────────────────────────────────────────

void RegimeManager::render(Renderer& renderer, const Universe& universe) {
    if (!regimes_[active_index_]) return;

    if (in_transition_) {
        renderer.setRegimeBlend(transition_from_, transition_to_, transition_t_);
        if (transition_t_ < 1.0f && regimes_[transition_from_]) {
            renderer.setRenderOpacity(std::clamp(1.0f - transition_t_, 0.0f, 1.0f));
            renderRegime(transition_from_, renderer, transition_from_universe_);
        }
        if (transition_t_ > 0.0f && regimes_[transition_to_]) {
            renderer.setRenderOpacity(std::clamp(transition_t_, 0.0f, 1.0f));
            renderRegime(transition_to_, renderer, universe);
        }
    } else {
        renderer.setRenderOpacity(1.0f);
        renderer.setRegimeBlend(active_index_, active_index_, 0.0f);
        renderRegime(active_index_, renderer, universe);
    }
    renderer.setRenderOpacity(1.0f);
}

IRegime* RegimeManager::getCurrentRegime() const {
    return regimes_[active_index_].get();
}

void RegimeManager::renderRegime(int regime_index, Renderer& renderer, const Universe& universe) const {
    if (regime_index < 0 || regime_index >= static_cast<int>(regimes_.size())) return;
    if (!regimes_[regime_index]) return;
    regimes_[regime_index]->render(renderer, universe);
}
