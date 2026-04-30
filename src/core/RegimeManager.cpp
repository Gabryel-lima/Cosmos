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
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
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

bool isQuarkFlavor(ParticleType type) {
    switch (type) {
        case ParticleType::QUARK_U:
        case ParticleType::QUARK_D:
        case ParticleType::QUARK_S:
        case ParticleType::QUARK_C:
        case ParticleType::QUARK_B:
        case ParticleType::QUARK_T:
        case ParticleType::ANTIQUARK_U:
        case ParticleType::ANTIQUARK_D:
        case ParticleType::ANTIQUARK_S:
        case ParticleType::ANTIQUARK_C:
        case ParticleType::ANTIQUARK_B:
        case ParticleType::ANTIQUARK_T:
        case ParticleType::ANTIQUARK:
            return true;
        default:
            return false;
    }
}

bool isAntiquarkFlavor(ParticleType type) {
    switch (type) {
        case ParticleType::ANTIQUARK_U:
        case ParticleType::ANTIQUARK_D:
        case ParticleType::ANTIQUARK_S:
        case ParticleType::ANTIQUARK_C:
        case ParticleType::ANTIQUARK_B:
        case ParticleType::ANTIQUARK_T:
        case ParticleType::ANTIQUARK:
            return true;
        default:
            return false;
    }
}

bool isLightQgpFlavor(ParticleType type) {
    switch (type) {
        case ParticleType::QUARK_U:
        case ParticleType::QUARK_D:
        case ParticleType::QUARK_S:
        case ParticleType::ANTIQUARK_U:
        case ParticleType::ANTIQUARK_D:
        case ParticleType::ANTIQUARK_S:
        case ParticleType::ANTIQUARK:
            return true;
        default:
            return false;
    }
}

bool isChargedLeptonType(ParticleType type) {
    switch (type) {
        case ParticleType::ELECTRON:
        case ParticleType::POSITRON:
        case ParticleType::MUON:
        case ParticleType::ANTIMUON:
        case ParticleType::TAU:
        case ParticleType::ANTITAU:
            return true;
        default:
            return false;
    }
}

bool isNeutrinoFlavor(ParticleType type) {
    switch (type) {
        case ParticleType::NEUTRINO:
        case ParticleType::NEUTRINO_E:
        case ParticleType::ANTINEUTRINO_E:
        case ParticleType::NEUTRINO_MU:
        case ParticleType::ANTINEUTRINO_MU:
        case ParticleType::NEUTRINO_TAU:
        case ParticleType::ANTINEUTRINO_TAU:
            return true;
        default:
            return false;
    }
}

bool isElectroweakBoson(ParticleType type) {
    return type == ParticleType::PHOTON || type == ParticleType::W_BOSON_POS ||
           type == ParticleType::W_BOSON_NEG || type == ParticleType::Z_BOSON ||
           type == ParticleType::HIGGS_BOSON;
}

float particleChargeValue(ParticleType type) {
    switch (type) {
        case ParticleType::QUARK_U:
        case ParticleType::QUARK_C:
        case ParticleType::QUARK_T:
            return 2.0f / 3.0f;
        case ParticleType::QUARK_D:
        case ParticleType::QUARK_S:
        case ParticleType::QUARK_B:
            return -1.0f / 3.0f;
        case ParticleType::ANTIQUARK_U:
        case ParticleType::ANTIQUARK_C:
        case ParticleType::ANTIQUARK_T:
            return -2.0f / 3.0f;
        case ParticleType::ANTIQUARK_D:
        case ParticleType::ANTIQUARK_S:
        case ParticleType::ANTIQUARK_B:
        case ParticleType::ANTIQUARK:
            return 1.0f / 3.0f;
        case ParticleType::PROTON:
        case ParticleType::POSITRON:
        case ParticleType::ANTIMUON:
        case ParticleType::ANTITAU:
        case ParticleType::W_BOSON_POS:
            return 1.0f;
        case ParticleType::ELECTRON:
        case ParticleType::MUON:
        case ParticleType::TAU:
        case ParticleType::W_BOSON_NEG:
            return -1.0f;
        default:
            return 0.0f;
    }
}

void seedRelativisticQcdCharge(ParticlePool& pool, size_t idx, ParticleType type,
                               bool neutral_gluons, std::mt19937& rng)
{
    if (type == ParticleType::GLUON) {
        if (neutral_gluons) {
            pool.clearQcdCharge(idx);
        } else {
            QcdColor color = QcdColor::NONE;
            QcdColor anticolor = QcdColor::NONE;
            randomDirectionalGluon(rng, color, anticolor);
            pool.setQcdCharge(idx, color, anticolor);
        }
        return;
    }
    if (!isQuarkFlavor(type)) {
        pool.clearQcdCharge(idx);
        return;
    }
    QcdColor primary = randomQcdPrimary(rng);
    if (isAntiquarkFlavor(type)) {
        pool.setQcdCharge(idx, QcdColor::NONE, qcd::antiColor(primary));
    } else {
        pool.setQcdCharge(idx, primary);
    }
}

ParticleType collapseToQgpFlavor(ParticleType type, std::mt19937& rng) {
    if (isLightQgpFlavor(type) || type == ParticleType::GLUON || type == ParticleType::PHOTON ||
        isChargedLeptonType(type) || isNeutrinoFlavor(type)) {
        return type;
    }

    if (isQuarkFlavor(type)) {
        const bool anti = isAntiquarkFlavor(type);
        switch (rng() % 3u) {
            case 0: return anti ? ParticleType::ANTIQUARK_U : ParticleType::QUARK_U;
            case 1: return anti ? ParticleType::ANTIQUARK_D : ParticleType::QUARK_D;
            default:return anti ? ParticleType::ANTIQUARK_S : ParticleType::QUARK_S;
        }
    }

    if (type == ParticleType::MUON || type == ParticleType::TAU) return ParticleType::ELECTRON;
    if (type == ParticleType::ANTIMUON || type == ParticleType::ANTITAU) return ParticleType::POSITRON;
    if (type == ParticleType::W_BOSON_POS || type == ParticleType::W_BOSON_NEG ||
        type == ParticleType::Z_BOSON || type == ParticleType::HIGGS_BOSON) {
        return (rng() % 2u == 0u) ? ParticleType::PHOTON : ParticleType::GLUON;
    }
    return ParticleType::PHOTON;
}

ParticleType coolToLeptonicFlavor(ParticleType type, std::mt19937& rng) {
    switch (type) {
        case ParticleType::QUARK_T:
        case ParticleType::QUARK_B:
            return (rng() % 2u == 0u) ? ParticleType::QUARK_C : ParticleType::QUARK_S;
        case ParticleType::ANTIQUARK_T:
        case ParticleType::ANTIQUARK_B:
            return (rng() % 2u == 0u) ? ParticleType::ANTIQUARK_C : ParticleType::ANTIQUARK_S;
        case ParticleType::W_BOSON_POS:
            return (rng() % 2u == 0u) ? ParticleType::POSITRON : ParticleType::ANTIMUON;
        case ParticleType::W_BOSON_NEG:
            return (rng() % 2u == 0u) ? ParticleType::ELECTRON : ParticleType::MUON;
        case ParticleType::Z_BOSON:
            return (rng() % 2u == 0u) ? ParticleType::PHOTON : ParticleType::NEUTRINO_E;
        case ParticleType::HIGGS_BOSON:
            return (rng() % 2u == 0u) ? ParticleType::PHOTON : ParticleType::TAU;
        default:
            return type;
    }
}

void seedRelativisticSphere(ParticlePool& pool, int regime_index, double radius,
                            double min_separation, double velocity_sigma,
                            std::mt19937& rng)
{
    const RegimeConfig::RelativisticRecipeView recipe = RegimeConfig::relativisticRecipeForRegime(regime_index);
    const int total_target = RegimeConfig::relativisticTargetCount(regime_index);
    if (total_target <= 0 || recipe.data == nullptr || recipe.size == 0) return;

    std::normal_distribution<double> vel_dist(0.0, velocity_sigma);
    std::vector<int> quotas(recipe.size, 0);
    double total_weight = 0.0;
    for (std::size_t i = 0; i < recipe.size; ++i) total_weight += recipe.data[i].weight;
    int allocated = 0;
    for (std::size_t i = 0; i < recipe.size; ++i) {
        quotas[i] = static_cast<int>(std::floor((recipe.data[i].weight / total_weight) * total_target));
        allocated += quotas[i];
    }
    for (std::size_t i = 0; allocated < total_target; ++i, ++allocated) {
        quotas[i % quotas.size()] += 1;
    }

    auto randomPosInRelativisticSphere = [&](double& px, double& py, double& pz) {
        std::uniform_real_distribution<double> dist(-radius, radius);
        do {
            px = dist(rng);
            py = dist(rng);
            pz = dist(rng);
        } while (px * px + py * py + pz * pz > radius * radius);
    };

    auto randomPosWithClearance = [&](double& px, double& py, double& pz) {
        const double min_dist2 = min_separation * min_separation;
        for (int attempt = 0; attempt < 80; ++attempt) {
            randomPosInRelativisticSphere(px, py, pz);
            bool overlap = false;
            for (size_t i = 0; i < pool.x.size(); ++i) {
                if (!(pool.flags[i] & PF_ACTIVE)) continue;
                const double dx = pool.x[i] - px;
                const double dy = pool.y[i] - py;
                const double dz = pool.z[i] - pz;
                if (dx * dx + dy * dy + dz * dz < min_dist2) {
                    overlap = true;
                    break;
                }
            }
            if (!overlap) return;
        }
        randomPosInRelativisticSphere(px, py, pz);
    };

    for (std::size_t i = 0; i < recipe.size; ++i) {
        for (int count = 0; count < quotas[i]; ++count) {
            double px, py, pz;
            randomPosWithClearance(px, py, pz);
            float cr, cg, cb;
            ParticlePool::defaultColor(recipe.data[i].type, cr, cg, cb);
            size_t added = pool.add(px, py, pz,
                                    vel_dist(rng), vel_dist(rng), vel_dist(rng),
                                    chemistry::restMass(recipe.data[i].type), recipe.data[i].type,
                                    cr, cg, cb, particleChargeValue(recipe.data[i].type));
            seedRelativisticQcdCharge(pool, added, recipe.data[i].type, regime_index == 3, rng);
            pool.luminosity[added] = recipe.data[i].luminosity;
        }
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
        case 1: return std::sqrt(CosmicClock::T_INFLATION_END * CosmicClock::T_REHEATING_END);
        case 2: return std::sqrt(CosmicClock::T_REHEATING_END * CosmicClock::T_LEPTON_END);
        case 3: return std::sqrt(CosmicClock::T_LEPTON_END * CosmicClock::T_QGP_END);
        case 4: return std::sqrt(CosmicClock::T_QGP_END * CosmicClock::T_BBN_END);
        case 5: return std::sqrt(CosmicClock::T_BBN_END * CosmicClock::T_RECOMBINATION);
        case 6: return std::sqrt(CosmicClock::T_RECOMBINATION * CosmicClock::T_DARK_AGES);
        case 7: return std::sqrt(CosmicClock::T_DARK_AGES * CosmicClock::T_REIONIZATION);
        case 8: return CosmicClock::T_REIONIZATION * 0.15;
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
    // Inflation is intentionally visualized in 2D. When it transitions into
    // the QGP, reinterpret that 2D scalar map as an angular chart of a 3D
    // sphere, preserving density contrast and fluctuations while gaining the
    // missing spatial dimension for the next regime.
    if (to >= 1 && to <= 3 && previous.phi_NX > 0 && previous.phi_NY > 0 && !previous.phi_field.empty()) {
        const int NX = previous.phi_NX;
        const int NY = previous.phi_NY;
        const size_t Ncells = previous.phi_field.size();
        const double qgp_radius = RegimeConfig::relativisticSeedConfigForRegime(to).radius;

        auto clampIndex = [](int value, int upper) {
            return std::clamp(value, 0, std::max(upper - 1, 0));
        };

        auto phiAt = [&](int x, int y) {
            const int sx = clampIndex(x, NX);
            const int sy = clampIndex(y, NY);
            return static_cast<double>(previous.phi_field[static_cast<size_t>(sx + NX * sy)]);
        };

        auto phiDotAt = [&](int x, int y) {
            if (previous.phi_dot_field.size() != Ncells) return 0.0;
            const int sx = clampIndex(x, NX);
            const int sy = clampIndex(y, NY);
            return static_cast<double>(previous.phi_dot_field[static_cast<size_t>(sx + NX * sy)]);
        };

        double mean_phi = 0.0;
        for (float value : previous.phi_field) {
            mean_phi += static_cast<double>(value);
        }
        mean_phi /= static_cast<double>(Ncells);

        double variance_phi = 0.0;
        for (float value : previous.phi_field) {
            double centered = static_cast<double>(value) - mean_phi;
            variance_phi += centered * centered;
        }
        variance_phi /= static_cast<double>(Ncells);
        const double rms_phi = std::sqrt(std::max(variance_phi, 1e-12));

        // Weight angular regions by overdensity and 2D field variation, so the
        // QGP inherits the inflation map as a spherical chart rather than as a
        // literal 3D extrusion.
        std::vector<double> weights;
        weights.reserve(Ncells);
        double total = 0.0;
        for (int y = 0; y < NY; ++y) {
            for (int x = 0; x < NX; ++x) {
                const double phi = phiAt(x, y);
                const double contrast = (phi - mean_phi) / rms_phi;
                const double grad_x = 0.5 * (phiAt(x + 1, y) - phiAt(x - 1, y));
                const double grad_y = 0.5 * (phiAt(x, y + 1) - phiAt(x, y - 1));
                const double gradient_mag = std::sqrt(grad_x * grad_x + grad_y * grad_y) / rms_phi;
                const double momentum_mag = std::abs(phiDotAt(x, y)) / rms_phi;
                double w = 0.08 + std::max(0.0, contrast) + 0.35 * gradient_mag + 0.15 * momentum_mag;
                weights.push_back(w);
                total += w;
            }
        }
        if (total <= 0.0) {
            // Let the existing buildInitialState() fallback stand if the field is degenerate.
            source = previous.particles;
        } else {
            std::vector<double> cdf(weights.size());
            double c = 0.0;
            for (size_t i = 0; i < weights.size(); ++i) { c += weights[i]; cdf[i] = c; }

            std::mt19937 rng = simrng::makeStream("inflation-to-qgp");
            std::uniform_real_distribution<double> uni(0.0, cdf.back());
            std::normal_distribution<double> thermal_vel(0.0, 0.025);
            std::uniform_real_distribution<double> jitteru(-0.4, 0.4);
            std::uniform_real_distribution<double> signed_unit(-1.0, 1.0);

            auto sampleCellIndex = [&](size_t attempts = 4) -> size_t {
                for (size_t a = 0; a < attempts; ++a) {
                    double r = uni(rng);
                    auto it = std::lower_bound(cdf.begin(), cdf.end(), r);
                    size_t idx = static_cast<size_t>(std::distance(cdf.begin(), it));
                    if (idx < cdf.size()) return idx;
                }
                return static_cast<size_t>(std::min<size_t>(cdf.size() - 1, static_cast<size_t>(std::floor((uni(rng) / cdf.back()) * cdf.size()))));
            };

            auto cellToPosAndVelocity = [&](size_t idx,
                                            double& px, double& py, double& pz,
                                            double& vx, double& vy, double& vz) {
                const int j = static_cast<int>(idx / NX);
                const int i = static_cast<int>(idx % NX);
                const double u = (static_cast<double>(i) + 0.5) / static_cast<double>(NX);
                const double v = (static_cast<double>(j) + 0.5) / static_cast<double>(NY);
                     const double theta = 2.0 * M_PI * u;
                     const double dir_y_chart = 1.0 - 2.0 * v;
                     const double radial_xy = std::sqrt(std::max(0.0, 1.0 - dir_y_chart * dir_y_chart));
                     const double chart_x = radial_xy * std::cos(theta);
                     const double chart_y = dir_y_chart;
                     const double chart_z = radial_xy * std::sin(theta);

                const double phi = phiAt(i, j);
                const double contrast = (phi - mean_phi) / rms_phi;
                const double grad_x = 0.5 * (phiAt(i + 1, j) - phiAt(i - 1, j));
                const double grad_y = 0.5 * (phiAt(i, j + 1) - phiAt(i, j - 1));
                const double grad_len = std::sqrt(grad_x * grad_x + grad_y * grad_y);
                const double flow_u = (grad_len > 1e-8) ? (grad_x / grad_len) : 0.0;
                const double flow_v = (grad_len > 1e-8) ? (grad_y / grad_len) : 0.0;
                const double flow_strength = std::clamp(grad_len / rms_phi, 0.0, 3.0);

                const double rand_theta = 2.0 * M_PI * std::clamp(uni(rng) / cdf.back(), 0.0, 1.0);
                const double rand_y = signed_unit(rng);
                const double rand_xy = std::sqrt(std::max(0.0, 1.0 - rand_y * rand_y));
                const double rand_x = rand_xy * std::cos(rand_theta);
                const double rand_z = rand_xy * std::sin(rand_theta);
                const double random_mix = std::clamp(0.34 + 0.10 * flow_strength - 0.03 * contrast, 0.32, 0.58);
                const double chart_mix = 1.0 - random_mix;

                double dir_x = chart_x * chart_mix + rand_x * random_mix;
                double dir_y = chart_y * chart_mix + rand_y * random_mix;
                double dir_z = chart_z * chart_mix + rand_z * random_mix;
                const double dir_norm = std::sqrt(std::max(1e-12, dir_x * dir_x + dir_y * dir_y + dir_z * dir_z));
                dir_x /= dir_norm;
                dir_y /= dir_norm;
                dir_z /= dir_norm;

                const double radial_random = std::cbrt(std::clamp(uni(rng) / cdf.back(), 0.0, 1.0));
                const double radial_bias = std::clamp(contrast * 0.12, -0.08, 0.16);
                const double radial_turbulence = 0.025 * jitteru(rng) * std::clamp(flow_strength, 0.0, 2.0);
                const double radius = qgp_radius * std::clamp(0.12 + 0.70 * std::pow(radial_random, 1.35) - radial_bias + radial_turbulence,
                                                              0.05,
                                                              0.90);

                double ref_x = 0.0;
                double ref_y = (std::abs(dir_y) < 0.92) ? 1.0 : 0.0;
                double ref_z = (std::abs(dir_y) < 0.92) ? 0.0 : 1.0;
                double tangent_u_x = ref_y * dir_z - ref_z * dir_y;
                double tangent_u_y = ref_z * dir_x - ref_x * dir_z;
                double tangent_u_z = ref_x * dir_y - ref_y * dir_x;
                const double tangent_u_norm = std::sqrt(std::max(1e-12,
                    tangent_u_x * tangent_u_x + tangent_u_y * tangent_u_y + tangent_u_z * tangent_u_z));
                tangent_u_x /= tangent_u_norm;
                tangent_u_y /= tangent_u_norm;
                tangent_u_z /= tangent_u_norm;
                const double tangent_v_x = dir_y * tangent_u_z - dir_z * tangent_u_y;
                const double tangent_v_y = dir_z * tangent_u_x - dir_x * tangent_u_z;
                const double tangent_v_z = dir_x * tangent_u_y - dir_y * tangent_u_x;

                const double cell_jitter = qgp_radius * (0.016 + 0.008 * std::clamp(flow_strength, 0.0, 1.4));
                px = dir_x * radius + rand_x * qgp_radius * 0.016 + cell_jitter * jitteru(rng);
                py = dir_y * radius + rand_y * qgp_radius * 0.016 + cell_jitter * jitteru(rng);
                pz = dir_z * radius + rand_z * qgp_radius * 0.016 + cell_jitter * jitteru(rng);

                const double inflow = std::clamp(contrast, -2.5, 3.0) * 0.014;
                const double phi_momentum = std::clamp(phiDotAt(i, j) / rms_phi, -2.5, 2.5) * 0.008;
                const double tangential_flow = std::clamp(0.0025 * flow_strength, 0.0, 0.0055);
                const double turbulent_kick = 0.008 * std::clamp(0.55 + 0.35 * flow_strength, 0.0, 1.6);
                const double ortho_noise_u = 0.0016 * jitteru(rng);
                const double ortho_noise_v = 0.0016 * jitteru(rng);
                vx = thermal_vel(rng) - dir_x * inflow + dir_x * phi_momentum
                   + tangent_u_x * (tangential_flow * flow_u + ortho_noise_u)
                   + tangent_v_x * (tangential_flow * flow_v + ortho_noise_v)
                   + rand_x * turbulent_kick;
                vy = thermal_vel(rng) - dir_y * inflow + dir_y * phi_momentum
                   + tangent_u_y * (tangential_flow * flow_u + ortho_noise_u)
                   + tangent_v_y * (tangential_flow * flow_v + ortho_noise_v)
                   + rand_y * turbulent_kick;
                vz = thermal_vel(rng) - dir_z * inflow + dir_z * phi_momentum
                   + tangent_u_z * (tangential_flow * flow_u + ortho_noise_u)
                   + tangent_v_z * (tangential_flow * flow_v + ortho_noise_v)
                   + rand_z * turbulent_kick;
            };

            auto randomPosInQgpSphere = [&](double& px, double& py, double& pz) {
                std::uniform_real_distribution<double> dist(-qgp_radius, qgp_radius);
                do {
                    px = dist(rng);
                    py = dist(rng);
                    pz = dist(rng);
                } while (px * px + py * py + pz * pz > qgp_radius * qgp_radius);
            };

            auto randomPosInQgpSphereWithClearance = [&](double min_distance,
                                                         double& px, double& py, double& pz) {
                const double min_distance2 = min_distance * min_distance;
                for (int attempt = 0; attempt < 96; ++attempt) {
                    randomPosInQgpSphere(px, py, pz);
                    bool overlaps = false;
                    for (size_t i = 0; i < next.x.size(); ++i) {
                        if (!(next.flags[i] & PF_ACTIVE)) continue;
                        double dx = next.x[i] - px;
                        double dy = next.y[i] - py;
                        double dz = next.z[i] - pz;
                        if (dx * dx + dy * dy + dz * dz < min_distance2) {
                            overlaps = true;
                            break;
                        }
                    }
                    if (!overlaps) return;
                }
                randomPosInQgpSphere(px, py, pz);
            };

            const RegimeConfig::RelativisticRecipeView recipe = RegimeConfig::relativisticRecipeForRegime(to);
            const int total_target = RegimeConfig::relativisticTargetCount(to);
            std::vector<int> quotas(recipe.size, 0);
            double total_weight = 0.0;
            for (std::size_t i = 0; i < recipe.size; ++i) total_weight += recipe.data[i].weight;
            int allocated = 0;
            for (std::size_t i = 0; i < recipe.size; ++i) {
                quotas[i] = static_cast<int>(std::floor((recipe.data[i].weight / total_weight) * total_target));
                allocated += quotas[i];
            }
            for (std::size_t i = 0; allocated < total_target; ++i, ++allocated) {
                quotas[i % quotas.size()] += 1;
            }

            const double min_dist2 = RegimeConfig::QGP_INIT_MIN_SEPARATION * RegimeConfig::QGP_INIT_MIN_SEPARATION;
            for (std::size_t ri = 0; ri < recipe.size; ++ri) {
                for (int count = 0; count < quotas[ri]; ++count) {
                    double px, py, pz, vx, vy, vz;
                    bool placed = false;
                    for (int attempt = 0; attempt < 32 && !placed; ++attempt) {
                        size_t cidx = sampleCellIndex();
                        cellToPosAndVelocity(cidx, px, py, pz, vx, vy, vz);
                        bool overlap = false;
                        for (size_t e = 0; e < next.x.size(); ++e) {
                            if (!(next.flags[e] & PF_ACTIVE)) continue;
                            double dx = next.x[e] - px;
                            double dy = next.y[e] - py;
                            double dz = next.z[e] - pz;
                            if (dx*dx + dy*dy + dz*dz < min_dist2) { overlap = true; break; }
                        }
                        if (!overlap) placed = true;
                    }
                    if (!placed) {
                        randomPosInQgpSphereWithClearance(RegimeConfig::QGP_INIT_MIN_SEPARATION, px, py, pz);
                        vx = thermal_vel(rng);
                        vy = thermal_vel(rng);
                        vz = thermal_vel(rng);
                    }

                    float cr, cg, cb;
                    ParticlePool::defaultColor(recipe.data[ri].type, cr, cg, cb);
                    size_t added = next.add(px, py, pz, vx, vy, vz,
                                            chemistry::restMass(recipe.data[ri].type), recipe.data[ri].type,
                                            cr, cg, cb, particleChargeValue(recipe.data[ri].type));
                    seedRelativisticQcdCharge(next, added, recipe.data[ri].type, to == 3, rng);
                    next.luminosity[added] = recipe.data[ri].luminosity;
                }
            }

            return next;
        }
    }
    if (to == 4) {
        for (size_t i = 0; i < source.x.size(); ++i) {
            if (!(source.flags[i] & PF_ACTIVE)) continue;
            if (isAntiquarkFlavor(source.type[i])) {
                source.type[i] = ParticleType::PHOTON;
                source.mass[i] = 0.0;
                source.charge[i] = 0.0f;
                source.clearQcdCharge(i);
            } else if (isQuarkFlavor(source.type[i]) && !isLightQgpFlavor(source.type[i])) {
                source.type[i] = collapseToQgpFlavor(source.type[i], initRng());
                source.mass[i] = chemistry::restMass(source.type[i]);
                source.charge[i] = particleChargeValue(source.type[i]);
                seedRelativisticQcdCharge(source, i, source.type[i], true, initRng());
            }
        }
        chemistry::hadronizeQgp(source);
    }
    const ParticlePool& src = source;
    if (src.x.empty()) return next;

    std::vector<size_t> plasma_baryons;

    for (size_t i = 0; i < src.x.size(); ++i) {
        if (!(src.flags[i] & PF_ACTIVE)) continue;

        switch (to) {
            case 2:
            case 3: {
                ParticleType mapped = src.type[i];
                if (to == 2) {
                    mapped = coolToLeptonicFlavor(mapped, initRng());
                } else {
                    mapped = collapseToQgpFlavor(mapped, initRng());
                }
                if (mapped == ParticleType::HIGGS_BOSON) mapped = ParticleType::PHOTON;
                size_t added = addParticleCopy(next, src, i, mapped, 0.0015f,
                                               (to == 2) ? std::max(0.9f, src.luminosity[i]) : std::max(0.8f, src.luminosity[i]),
                                               particleChargeValue(mapped));
                seedRelativisticQcdCharge(next, added, mapped, to == 3, initRng());
                break;
            }

            case 4: {
                if (!chemistry::isLightNucleus(src.type[i])) continue;
                addParticleCopy(next, src, i, src.type[i], 0.0f, src.luminosity[i], src.charge[i]);
                break;
            }

            case 5: {
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

            case 6:
            case 7:
            case 8: {
                ParticleType mapped = src.type[i];
                if (mapped == ParticleType::PHOTON || mapped == ParticleType::NEUTRINO) continue;
                if (isNeutrinoFlavor(mapped)) continue;
                if (mapped == ParticleType::ELECTRON || isBaryonicType(mapped)) {
                    if (mapped != ParticleType::STAR && mapped != ParticleType::BLACKHOLE) {
                        mapped = ParticleType::GAS;
                    }
                } else if (mapped == ParticleType::POSITRON || mapped == ParticleType::MUON || mapped == ParticleType::ANTIMUON ||
                           mapped == ParticleType::TAU || mapped == ParticleType::ANTITAU || isQuarkFlavor(mapped) ||
                           mapped == ParticleType::GLUON || isElectroweakBoson(mapped)) {
                    mapped = ParticleType::GAS;
                }
                size_t added = addParticleCopy(next, src, i, mapped);
                if (mapped == ParticleType::GAS || mapped == ParticleType::DARK_MATTER) {
                    next.mass[added] = (mapped == ParticleType::DARK_MATTER) ? RegimeConfig::MASS_DARK_MATTER : RegimeConfig::MASS_GAS;
                    if (mapped == ParticleType::GAS) {
                        next.charge[added] = 0.0f;
                        next.luminosity[added] = (to == 6) ? 0.18f : (to == 7 ? 0.35f : 0.8f);
                    }
                }

                size_t star_step = (to == 7) ? RegimeConfig::TRANS_STRUCT_STAR_SPAWN_STEP * 2
                                             : RegimeConfig::TRANS_STRUCT_STAR_SPAWN_STEP;
                if (to >= 7 && mapped == ParticleType::GAS && i % std::max<size_t>(star_step, 1) == 0) {
                    size_t star = next.add(src.x[i], src.y[i], src.z[i],
                                           src.vx[i], src.vy[i], src.vz[i],
                                           RegimeConfig::MASS_STAR, ParticleType::STAR,
                                           (to == 7) ? 0.82f : 1.0f,
                                           (to == 7) ? 0.9f  : 0.92f,
                                           (to == 7) ? 1.0f  : 0.72f,
                                           0.0f);
                    next.star_state[star] = (to == 7) ? StarState::MAIN_SEQUENCE : StarState::PROTOSTAR;
                    next.luminosity[star] = (to == 7) ? 4.8f : 4.0f;
                    next.flags[star] |= PF_STAR_FORMED;
                }
                if (to == 8 && mapped == ParticleType::GAS && i % (RegimeConfig::TRANS_STRUCT_STAR_SPAWN_STEP * 12) == 0) {
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

    if (to == 5 && !plasma_baryons.empty()) {
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
        const bool preserve_derived_counts = (to == 4 || to == 5);

        if (to < 4 && !preserve_derived_counts && target_size > 0 && target_size != activeParticleCount(remapped)) {
            remapped = resampleParticlePoolLOD(remapped, target_size);
        }

        if (to == 4 || to == 5) {
            next.abundances = chemistry::inferAbundances(remapped);
        }

        if (to >= 6) {
            imprintStructureTemplate(next.particles, remapped, to);
        } else if (activeParticleCount(remapped) > 0) {
            next.particles = std::move(remapped);
        }
    }

    if (to >= 5 && previous.density_field.NX > 0 && previous.density_field.data.size() == next.field.data.size()) {
        next.field = previous.density_field;
    }
    if (to >= 6 && previous.ionization_field.NX > 0 && previous.ionization_field.data.size() == next.ionization_field.data.size()) {
        next.ionization_field = previous.ionization_field;
    }
    if (to >= 6 && previous.emissivity_field.NX > 0 && previous.emissivity_field.data.size() == next.emissivity_field.data.size()) {
        next.emissivity_field = previous.emissivity_field;
    }
}

} // namespace

// ── Construção ─────────────────────────────────────────────────────────────

RegimeManager::RegimeManager() {
    regimes_[0] = std::make_unique<RegimeInflation>();
    regimes_[1] = std::make_unique<RegimeQGP>();
    regimes_[2] = std::make_unique<RegimeQGP>();
    regimes_[3] = std::make_unique<RegimeQGP>();
    regimes_[4] = std::make_unique<RegimeNucleosynthesis>();
    regimes_[5] = std::make_unique<RegimePlasma>();
    regimes_[6] = std::make_unique<RegimeStructure>(StructurePhase::DARK_AGES);
    regimes_[7] = std::make_unique<RegimeStructure>(StructurePhase::REIONIZATION);
    regimes_[8] = std::make_unique<RegimeStructure>(StructurePhase::MATURE);
}

// ── Construtores de Estado Inicial ─────────────────────────────────────────────────────

struct StructureMode {
    glm::dvec3 wave;
    double amplitude;
    double phase;
};

double fractUnit(double value) {
    return value - std::floor(value);
}

double wrapPeriodic(double value, double box_size) {
    if (box_size <= 0.0) return value;
    double wrapped = std::fmod(value + box_size * 0.5, box_size);
    if (wrapped < 0.0) wrapped += box_size;
    return wrapped - box_size * 0.5;
}

double cellHash01(int i, int j, int k, unsigned int stream) {
    unsigned int h = static_cast<unsigned int>(i) * 0x8da6b343u;
    h ^= static_cast<unsigned int>(j) * 0xd8163841u;
    h ^= static_cast<unsigned int>(k) * 0xcb1ab31fu;
    h ^= stream * 0x9e3779b9u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return static_cast<double>(h) / 4294967295.0;
}

struct StructureLptSample {
    glm::dvec3 displacement{0.0};
    glm::dvec3 velocity{0.0};
    float overdensity = 0.0f;
};

std::array<StructureMode, 10> makeStructureModes(std::mt19937& rng) {
    constexpr double tau = 6.28318530717958647692;
    std::uniform_int_distribution<int> kdist(1, 5);
    std::uniform_real_distribution<double> amp_dist(0.015, 0.075);
    std::uniform_real_distribution<double> phase_dist(0.0, tau);
    std::bernoulli_distribution sign_flip(0.5);

    std::array<StructureMode, 10> modes{};
    for (StructureMode& mode : modes) {
        glm::dvec3 wave(static_cast<double>(kdist(rng)),
                        static_cast<double>(kdist(rng)),
                        static_cast<double>(kdist(rng)));
        if (sign_flip(rng)) wave.x *= -1.0;
        if (sign_flip(rng)) wave.y *= -1.0;
        if (sign_flip(rng)) wave.z *= -1.0;
        mode.wave = wave;
        mode.amplitude = amp_dist(rng) / std::sqrt(glm::dot(wave, wave));
        mode.phase = phase_dist(rng);
    }
    return modes;
}

StructureLptSample sampleStructureLpt(const glm::dvec3& uvw,
                                      const std::array<StructureMode, 10>& modes)
{
    constexpr double tau = 6.28318530717958647692;
    std::array<double, 10> theta{};
    std::array<glm::dvec3, 10> gradients{};

    glm::dvec3 grad1(0.0);
    double density = 0.0;
    for (std::size_t index = 0; index < modes.size(); ++index) {
        const StructureMode& mode = modes[index];
        theta[index] = tau * glm::dot(mode.wave, uvw) + mode.phase;
        const double s = std::sin(theta[index]);
        const double c = std::cos(theta[index]);
        gradients[index] = mode.wave * (mode.amplitude * tau * c);
        grad1 += gradients[index];
        density += mode.amplitude * glm::dot(mode.wave, mode.wave) * s;
    }

    glm::dvec3 grad2(0.0);
    for (std::size_t a = 0; a < modes.size(); ++a) {
        for (std::size_t b = a + 1; b < modes.size(); ++b) {
            const double coupling = 0.20 * modes[a].amplitude * modes[b].amplitude;
            const glm::dvec3 delta_k = modes[a].wave - modes[b].wave;
            grad2 += delta_k * (coupling * std::sin(theta[a] - theta[b]));
        }
    }

    StructureLptSample sample;
    sample.displacement = (-0.0021 * grad1) + (-0.00055 * grad2);
    sample.velocity = (0.11 * sample.displacement) + (-0.00018 * grad2);
    sample.overdensity = std::clamp(static_cast<float>(0.5 + density * 0.42), -1.0f, 1.0f);
    return sample;
}

void seedStructureFields(GridData& density_field,
                         GridData& ionization_field,
                         GridData& emissivity_field,
                         const std::array<StructureMode, 10>& modes,
                         int regime_index)
{
    const int N = density_field.NX;
    const float phase_seed = static_cast<float>(0.18 * regime_index + 0.37);
    for (int z = 0; z < N; ++z)
    for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
        const glm::dvec3 uvw((static_cast<double>(x) + 0.5) / static_cast<double>(N),
                             (static_cast<double>(y) + 0.5) / static_cast<double>(N),
                             (static_cast<double>(z) + 0.5) / static_cast<double>(N));
        const StructureLptSample lpt = sampleStructureLpt(uvw, modes);
        const float overdensity01 = std::clamp(0.5f + 0.5f * lpt.overdensity, 0.0f, 1.0f);
        const float filament = std::clamp(0.25f + 0.75f * overdensity01, 0.0f, 1.0f);
        const float patch = 0.5f + 0.5f * std::sin(6.2831853f * (0.61f * static_cast<float>(uvw.x)
                                                                + 0.37f * static_cast<float>(uvw.y)
                                                                + 0.29f * static_cast<float>(uvw.z))
                                                   + phase_seed);
        const float emissive_seed = std::pow(std::max(overdensity01 * patch, 0.0f), 1.8f);

        density_field.at(x, y, z) = 0.010f + filament * 0.055f;
        if (regime_index == 6) {
            ionization_field.at(x, y, z) = emissive_seed * 0.025f;
            emissivity_field.at(x, y, z) = emissive_seed * 0.015f;
        } else if (regime_index == 7) {
            ionization_field.at(x, y, z) = std::clamp(emissive_seed * 0.42f + patch * 0.08f, 0.0f, 0.55f);
            emissivity_field.at(x, y, z) = emissive_seed * 0.22f;
        } else {
            ionization_field.at(x, y, z) = std::clamp(0.28f + emissive_seed * 0.46f + patch * 0.10f, 0.0f, 1.0f);
            emissivity_field.at(x, y, z) = emissive_seed * 0.30f;
        }
    }
}

/// Aproximação LPT coerente: desloca grade regular com modos de potencial de larga escala
/// e uma correção de segunda ordem inspirada em 2LPT/COLA. Produz condições iniciais
/// mais plausíveis para a formação de estrutura tardia do que jitter gaussiano puro.
static void zelDovichDisplace(ParticlePool& pool, GridData& density_field,
                              GridData& ionization_field, GridData& emissivity_field,
                              int N_cbrt, double box_size, int regime_index) {
    std::mt19937& rng_init = initRng();
    const auto modes = makeStructureModes(rng_init);
    pool.clear();
    std::uniform_real_distribution<double> global_shift_dist(0.0, 1.0);
    const glm::dvec3 global_shift(global_shift_dist(rng_init),
                                  global_shift_dist(rng_init),
                                  global_shift_dist(rng_init));
    const double jitter_cells = 0.38;

    seedStructureFields(density_field, ionization_field, emissivity_field, modes, regime_index);

    for (int k = 0; k < N_cbrt; ++k)
    for (int j = 0; j < N_cbrt; ++j)
    for (int i = 0; i < N_cbrt; ++i) {
        const glm::dvec3 cell_jitter((cellHash01(i, j, k, 11u) - 0.5) * jitter_cells,
                                     (cellHash01(i, j, k, 23u) - 0.5) * jitter_cells,
                                     (cellHash01(i, j, k, 37u) - 0.5) * jitter_cells);
        const glm::dvec3 uvw(fractUnit((static_cast<double>(i) + 0.5 + cell_jitter.x) / static_cast<double>(N_cbrt) + global_shift.x),
                             fractUnit((static_cast<double>(j) + 0.5 + cell_jitter.y) / static_cast<double>(N_cbrt) + global_shift.y),
                             fractUnit((static_cast<double>(k) + 0.5 + cell_jitter.z) / static_cast<double>(N_cbrt) + global_shift.z));
        const StructureLptSample lpt = sampleStructureLpt(uvw, modes);
        const double x = wrapPeriodic((uvw.x - 0.5 + lpt.displacement.x) * box_size, box_size);
        const double y = wrapPeriodic((uvw.y - 0.5 + lpt.displacement.y) * box_size, box_size);
        const double z = wrapPeriodic((uvw.z - 0.5 + lpt.displacement.z) * box_size, box_size);

        const float overdensity01 = std::clamp(0.5f + 0.5f * lpt.overdensity, 0.0f, 1.0f);
        const bool gas_biased = (cellHash01(i, j, k, 53u) < (1.0 / static_cast<double>(RegimeConfig::STRUCT_GAS_RATIO_DIVISOR)))
                             || (overdensity01 > 0.72f && cellHash01(i, j, k, 71u) < 0.24);
        const ParticleType t = gas_biased ? ParticleType::GAS : ParticleType::DARK_MATTER;
        const double mass = (t == ParticleType::DARK_MATTER) ? RegimeConfig::MASS_DARK_MATTER : RegimeConfig::MASS_GAS;

        float cr, cg, cb;
        ParticlePool::defaultColor(t, cr, cg, cb);
        if (t == ParticleType::GAS) {
            cr = std::clamp(cr * (0.85f + overdensity01 * 0.25f), 0.0f, 1.0f);
            cg = std::clamp(cg * (0.95f + overdensity01 * 0.15f), 0.0f, 1.0f);
            cb = std::clamp(cb * (1.00f + overdensity01 * 0.18f), 0.0f, 1.0f);
        }

        size_t added = pool.add(x, y, z,
                                lpt.velocity.x, lpt.velocity.y, lpt.velocity.z,
                                mass, t, cr, cg, cb);
        pool.luminosity[added] = (t == ParticleType::GAS)
            ? std::clamp(0.16f + overdensity01 * 0.55f, 0.12f, 0.85f)
            : std::clamp(0.03f + overdensity01 * 0.08f, 0.02f, 0.14f);
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
        case 3: cam.ortho = false; cam.zoom = 3.2; break;
        case 4: cam.ortho = false; cam.zoom = 7.5; break;
        case 5: cam.ortho = false; cam.zoom = 38.0; break;
        case 6: cam.ortho = false; cam.zoom = 45.0; break;
        case 7: cam.ortho = false; cam.zoom = 52.0; break;
        case 8: cam.ortho = false; cam.zoom = 58.0; break;
    }
    cam.pos_z = static_cast<double>(cam.zoom) * 1.5;
    st.suggested_camera = cam;

    // Preencher partículas ────────────────────────────────────────────────────
    switch (idx) {
        case 0:
            // Inflação: sem partículas; campo escalar inicializado por RegimeInflation::onEnter
            break;

        case 1: {
            const auto cfg = RegimeConfig::relativisticSeedConfigForRegime(idx);
            seedRelativisticSphere(st.particles, idx, cfg.radius,
                                   RegimeConfig::QGP_INIT_MIN_SEPARATION * cfg.min_separation_scale,
                                   cfg.velocity_sigma, rng_init);
            break;
        }

        case 2: {
            const auto cfg = RegimeConfig::relativisticSeedConfigForRegime(idx);
            seedRelativisticSphere(st.particles, idx, cfg.radius,
                                   RegimeConfig::QGP_INIT_MIN_SEPARATION * cfg.min_separation_scale,
                                   cfg.velocity_sigma, rng_init);
            break;
        }

        case 3: {
            const auto cfg = RegimeConfig::relativisticSeedConfigForRegime(idx);
            seedRelativisticSphere(st.particles, idx, cfg.radius,
                                   RegimeConfig::QGP_INIT_MIN_SEPARATION * cfg.min_separation_scale,
                                   cfg.velocity_sigma, rng_init);
            break;
        }

        case 4: {
            // NBB: prótons e nêutrons (da hadronização)
            st.abundances = NuclearAbundances{};
            st.abundances.Xp = RegimeConfig::BBN_INIT_XP; 
            st.abundances.Xn = RegimeConfig::BBN_INIT_XN;
            int N = RegimeConfig::BBN_NUCLEON_COUNT;
            std::normal_distribution<double> vel_dist(0.0, 0.01);
            std::bernoulli_distribution proton_dist(RegimeConfig::BBN_INIT_XP);
            for (int i = 0; i < N; ++i) {
                double px, py, pz;
                randomPosInSphereWithClearance(0.5, RegimeConfig::BBN_INIT_MIN_SEPARATION,
                                               st.particles, px, py, pz);
                bool is_proton = proton_dist(rng_init);
                ParticleType t = is_proton ? ParticleType::PROTON : ParticleType::NEUTRON;
                float cr, cg, cb; ParticlePool::defaultColor(t, cr, cg, cb);
                size_t added = st.particles.add(px, py, pz,
                                                vel_dist(rng_init), vel_dist(rng_init), vel_dist(rng_init),
                                                phys::m_p, t, cr, cg, cb);
                st.particles.luminosity[added] = is_proton ? 1.7f : 1.5f;
            }
            break;
        }

        case 5: {
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

        case 6:
        case 7:
        case 8: {
            // Estruturas tardias: grade com deslocamento LPT coerente em z~20.
            int N_cbrt = RegimeConfig::STRUCT_ZELDOVICH_N_CBRT;  // ~15 625 partículas (gerenciável para formação estelar)
            double box = RegimeConfig::STRUCT_BOX_SIZE_MPC;  // Mpc comóvel (câmera vê melhor)
            st.field.resize(RegimeConfig::STRUCT_GRID_SIZE, RegimeConfig::STRUCT_GRID_SIZE, RegimeConfig::STRUCT_GRID_SIZE);
            st.ionization_field.resize(RegimeConfig::STRUCT_GRID_SIZE, RegimeConfig::STRUCT_GRID_SIZE, RegimeConfig::STRUCT_GRID_SIZE);
            st.emissivity_field.resize(RegimeConfig::STRUCT_GRID_SIZE, RegimeConfig::STRUCT_GRID_SIZE, RegimeConfig::STRUCT_GRID_SIZE);
            zelDovichDisplace(st.particles, st.field, st.ionization_field, st.emissivity_field,
                              N_cbrt, box, idx);

            for (size_t i = 0; i < st.particles.x.size(); ++i) {
                if (st.particles.type[i] == ParticleType::GAS) {
                    st.particles.luminosity[i] = (idx == 6) ? 0.06f : (idx == 7 ? 0.12f : 0.20f);
                    if (idx == 6) {
                        st.particles.color_r[i] = 0.34f; st.particles.color_g[i] = 0.22f; st.particles.color_b[i] = 0.11f;
                    } else if (idx == 7) {
                        st.particles.color_r[i] = 0.42f; st.particles.color_g[i] = 0.28f; st.particles.color_b[i] = 0.15f;
                    } else {
                        st.particles.color_r[i] = 0.50f; st.particles.color_g[i] = 0.32f; st.particles.color_b[i] = 0.18f;
                    }
                } else if (st.particles.type[i] == ParticleType::DARK_MATTER) {
                    st.particles.color_r[i] = 0.18f; st.particles.color_g[i] = 0.14f; st.particles.color_b[i] = 0.20f;
                    st.particles.luminosity[i] = 0.03f;
                }
            }

            if (idx >= 7) {
                const size_t star_step = (idx == 7) ? RegimeConfig::STRUCT_STAR_SPAWN_STEP * 2
                                                    : RegimeConfig::STRUCT_STAR_SPAWN_STEP;
                for (size_t i = 0; i < st.particles.x.size(); i += star_step) {
                    if (st.particles.type[i] != ParticleType::GAS) continue;
                    size_t star = st.particles.add(st.particles.x[i], st.particles.y[i], st.particles.z[i],
                                                   st.particles.vx[i], st.particles.vy[i], st.particles.vz[i],
                                                   RegimeConfig::MASS_STAR, ParticleType::STAR,
                                                   1.0f,
                                                   (idx == 7) ? 0.84f : 0.92f,
                                                   (idx == 7) ? 0.60f : 0.78f,
                                                   0.0f);
                    st.particles.star_state[star] = (idx == 7) ? StarState::PROTOSTAR : StarState::MAIN_SEQUENCE;
                    st.particles.luminosity[star] = (idx == 7) ? 2.4f : 3.4f;
                    st.particles.flags[star] |= PF_STAR_FORMED;
                }
            }

            if (idx == 8) {
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
    if (regime_index >= 6) {
        universe.ionization_field = std::move(state.ionization_field);
        universe.emissivity_field = std::move(state.emissivity_field);
        if (universe.ionization_field.NX == 0 && universe.density_field.NX > 0) {
            universe.ionization_field.resize(universe.density_field.NX,
                                             universe.density_field.NY,
                                             universe.density_field.NZ);
        }
        if (universe.emissivity_field.NX == 0 && universe.density_field.NX > 0) {
            universe.emissivity_field.resize(universe.density_field.NX,
                                             universe.density_field.NY,
                                             universe.density_field.NZ);
        }
    } else {
        universe.ionization_field = GridData{};
        universe.emissivity_field = GridData{};
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
