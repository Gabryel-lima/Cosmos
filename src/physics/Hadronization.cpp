#include "Hadronization.hpp"

#include "Constants.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace chemistry {
namespace {

double massFromMeV(double mev) {
    return mev * phys::MeV / (phys::c * phys::c);
}

constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();

double distanceSquared(const ParticlePool& pool, size_t a, size_t b) {
    const double dx = pool.x[a] - pool.x[b];
    const double dy = pool.y[a] - pool.y[b];
    const double dz = pool.z[a] - pool.z[b];
    return dx * dx + dy * dy + dz * dz;
}

double distanceSquaredPoint(const ParticlePool& pool, size_t i, double px, double py, double pz) {
    const double dx = pool.x[i] - px;
    const double dy = pool.y[i] - py;
    const double dz = pool.z[i] - pz;
    return dx * dx + dy * dy + dz * dz;
}

size_t nearestCandidate(const ParticlePool& pool,
                        const std::vector<size_t>& candidates,
                        const std::vector<uint8_t>& used,
                        size_t anchor,
                        size_t exclude,
                        double max_distance_sq)
{
    size_t best = kInvalidIndex;
    double best_distance = max_distance_sq;
    for (size_t idx : candidates) {
        if (idx == exclude || used[idx]) continue;
        const double d2 = distanceSquared(pool, anchor, idx);
        if (d2 < best_distance) {
            best_distance = d2;
            best = idx;
        }
    }
    return best;
}

size_t nearestGluon(const ParticlePool& pool,
                    const std::vector<size_t>& gluons,
                    const std::vector<uint8_t>& used,
                    double px,
                    double py,
                    double pz,
                    double max_distance_sq)
{
    size_t best = kInvalidIndex;
    double best_distance = max_distance_sq;
    for (size_t idx : gluons) {
        if (used[idx]) continue;
        if (!(pool.flags[idx] & PF_ACTIVE)) continue;
        const double d2 = distanceSquaredPoint(pool, idx, px, py, pz);
        if (d2 < best_distance) {
            best_distance = d2;
            best = idx;
        }
    }
    return best;
}

struct TripletCandidate {
    size_t first = kInvalidIndex;
    size_t second = kInvalidIndex;
    size_t third = kInvalidIndex;
    size_t gluon = kInvalidIndex;
    double score = std::numeric_limits<double>::infinity();
    bool valid = false;
};

TripletCandidate findBestTriplet(const ParticlePool& pool,
                                 const std::vector<size_t>& primary,
                                 const std::vector<size_t>& same_kind,
                                 const std::vector<size_t>& other_kind,
                                 const std::vector<size_t>& gluons,
                                 const std::vector<uint8_t>& used,
                                 double max_radius)
{
    TripletCandidate best;
    const double max_distance_sq = std::isfinite(max_radius)
        ? max_radius * max_radius
        : std::numeric_limits<double>::infinity();

    for (size_t seed : primary) {
        if (used[seed]) continue;
        const size_t partner_same = nearestCandidate(pool, same_kind, used, seed, seed, max_distance_sq);
        if (partner_same == kInvalidIndex) continue;

        const double cx0 = (pool.x[seed] + pool.x[partner_same]) * 0.5;
        const double cy0 = (pool.y[seed] + pool.y[partner_same]) * 0.5;
        const double cz0 = (pool.z[seed] + pool.z[partner_same]) * 0.5;

        size_t partner_other = kInvalidIndex;
        double best_other = max_distance_sq;
        for (size_t idx : other_kind) {
            if (used[idx]) continue;
            const double d2 = distanceSquaredPoint(pool, idx, cx0, cy0, cz0);
            if (d2 < best_other) {
                best_other = d2;
                partner_other = idx;
            }
        }
        if (partner_other == kInvalidIndex) continue;

        const double cx = (pool.x[seed] + pool.x[partner_same] + pool.x[partner_other]) / 3.0;
        const double cy = (pool.y[seed] + pool.y[partner_same] + pool.y[partner_other]) / 3.0;
        const double cz = (pool.z[seed] + pool.z[partner_same] + pool.z[partner_other]) / 3.0;
        const size_t gluon = nearestGluon(pool, gluons, used, cx, cy, cz,
                                          std::isfinite(max_radius) ? max_distance_sq * 2.25
                                                                    : std::numeric_limits<double>::infinity());

        const double d_seed = distanceSquared(pool, seed, partner_same);
        const double d_other = distanceSquaredPoint(pool, partner_other, cx0, cy0, cz0);
        const double gluon_term = (gluon == kInvalidIndex) ? (std::isfinite(max_radius) ? max_radius * 0.75 : 1.0)
                                                           : std::sqrt(distanceSquaredPoint(pool, gluon, cx, cy, cz)) * 0.35;
        const double score = std::sqrt(d_seed) + std::sqrt(d_other) + gluon_term;
        if (score < best.score) {
            best.first = seed;
            best.second = partner_same;
            best.third = partner_other;
            best.gluon = gluon;
            best.score = score;
            best.valid = true;
        }
    }

    return best;
}

void applyHadronProperties(ParticlePool& pool, size_t idx, ParticleType type, bool gluon_supported) {
    pool.type[idx] = type;
    pool.mass[idx] = restMass(type);
    pool.charge[idx] = static_cast<float>(atomicCharge(type));
    pool.clearQcdCharge(idx);
    ParticlePool::defaultColor(type, pool.color_r[idx], pool.color_g[idx], pool.color_b[idx]);
    pool.luminosity[idx] = gluon_supported ? 2.4f : 1.8f;
    pool.flags[idx] |= PF_BOUND;
}

} // namespace

bool isLightNucleus(ParticleType type) {
    switch (type) {
        case ParticleType::PROTON:
        case ParticleType::NEUTRON:
        case ParticleType::DEUTERIUM:
        case ParticleType::HELIUM3:
        case ParticleType::HELIUM4NUCLEI:
        case ParticleType::LITHIUM7:
            return true;
        default:
            return false;
    }
}

bool isBaryonicParticle(ParticleType type) {
    switch (type) {
        case ParticleType::PROTON:
        case ParticleType::NEUTRON:
        case ParticleType::DEUTERIUM:
        case ParticleType::HELIUM3:
        case ParticleType::HELIUM4NUCLEI:
        case ParticleType::LITHIUM7:
        case ParticleType::GAS:
        case ParticleType::STAR:
        case ParticleType::BLACKHOLE:
            return true;
        default:
            return false;
    }
}

int baryonNumber(ParticleType type) {
    switch (type) {
        case ParticleType::PROTON:
        case ParticleType::NEUTRON:
            return 1;
        case ParticleType::DEUTERIUM:
            return 2;
        case ParticleType::HELIUM3:
            return 3;
        case ParticleType::HELIUM4NUCLEI:
            return 4;
        case ParticleType::LITHIUM7:
            return 7;
        case ParticleType::GAS:
            return 1;
        default:
            return 0;
    }
}

int atomicCharge(ParticleType type) {
    switch (type) {
        case ParticleType::QUARK_U:
        case ParticleType::QUARK_C:
        case ParticleType::QUARK_T:
            return 1;
        case ParticleType::ANTIQUARK_U:
        case ParticleType::ANTIQUARK_C:
        case ParticleType::ANTIQUARK_T:
            return -1;
        case ParticleType::QUARK_D:
        case ParticleType::QUARK_S:
        case ParticleType::QUARK_B:
            return 0;
        case ParticleType::ANTIQUARK_D:
        case ParticleType::ANTIQUARK_S:
        case ParticleType::ANTIQUARK_B:
            return 0;
        case ParticleType::PROTON:
        case ParticleType::DEUTERIUM:
            return 1;
        case ParticleType::HELIUM3:
        case ParticleType::HELIUM4NUCLEI:
            return 2;
        case ParticleType::LITHIUM7:
            return 3;
        case ParticleType::POSITRON:
        case ParticleType::ANTIMUON:
        case ParticleType::ANTITAU:
        case ParticleType::W_BOSON_POS:
            return 1;
        case ParticleType::ELECTRON:
        case ParticleType::MUON:
        case ParticleType::TAU:
        case ParticleType::W_BOSON_NEG:
            return -1;
        default:
            return 0;
    }
}

double restMass(ParticleType type) {
    switch (type) {
        case ParticleType::QUARK_U:
        case ParticleType::ANTIQUARK_U:
            return massFromMeV(2.2);
        case ParticleType::QUARK_D:
        case ParticleType::ANTIQUARK_D:
            return massFromMeV(4.7);
        case ParticleType::QUARK_S:
        case ParticleType::ANTIQUARK_S:
            return massFromMeV(93.0);
        case ParticleType::QUARK_C:
        case ParticleType::ANTIQUARK_C:
            return massFromMeV(1270.0);
        case ParticleType::QUARK_B:
        case ParticleType::ANTIQUARK_B:
            return massFromMeV(4180.0);
        case ParticleType::QUARK_T:
        case ParticleType::ANTIQUARK_T:
            return massFromMeV(172760.0);
        case ParticleType::ANTIQUARK:
            return massFromMeV(4.7);
        case ParticleType::PROTON:
            return phys::m_p;
        case ParticleType::NEUTRON:
            return phys::m_n;
        case ParticleType::DEUTERIUM:
            return phys::m_p * 2.0;
        case ParticleType::HELIUM3:
            return phys::m_p * 3.0;
        case ParticleType::HELIUM4NUCLEI:
            return phys::m_p * 4.0;
        case ParticleType::LITHIUM7:
            return phys::m_p * 7.0;
        case ParticleType::ELECTRON:
        case ParticleType::POSITRON:
            return phys::m_e;
        case ParticleType::MUON:
        case ParticleType::ANTIMUON:
            return massFromMeV(105.658);
        case ParticleType::TAU:
        case ParticleType::ANTITAU:
            return massFromMeV(1776.86);
        case ParticleType::W_BOSON_POS:
        case ParticleType::W_BOSON_NEG:
            return massFromMeV(80379.0);
        case ParticleType::Z_BOSON:
            return massFromMeV(91187.6);
        case ParticleType::HIGGS_BOSON:
            return massFromMeV(125250.0);
        case ParticleType::PHOTON:
        case ParticleType::GLUON:
        case ParticleType::NEUTRINO:
        case ParticleType::NEUTRINO_E:
        case ParticleType::ANTINEUTRINO_E:
        case ParticleType::NEUTRINO_MU:
        case ParticleType::ANTINEUTRINO_MU:
        case ParticleType::NEUTRINO_TAU:
        case ParticleType::ANTINEUTRINO_TAU:
            return 0.0;
        default:
            return phys::m_p;
    }
}

HadronizationStats hadronizeQgp(ParticlePool& pool, double binding_radius) {
    HadronizationStats stats;
    const size_t n = pool.x.size();
    if (n == 0) return stats;

    std::vector<size_t> up_like;
    std::vector<size_t> down_like;
    std::vector<size_t> strange;
    std::vector<size_t> gluons;
    for (size_t i = 0; i < n; ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        switch (pool.type[i]) {
            case ParticleType::QUARK_U: up_like.push_back(i); break;
            case ParticleType::QUARK_D: down_like.push_back(i); break;
            case ParticleType::QUARK_S: strange.push_back(i); break;
            case ParticleType::GLUON:   gluons.push_back(i); break;
            default: break;
        }
    }

    if (up_like.empty() && down_like.empty() && strange.empty()) {
        for (size_t idx : gluons) {
            if (pool.flags[idx] & PF_ACTIVE) {
                pool.deactivate(idx);
                ++stats.confined_gluons;
            }
        }
        return stats;
    }

    const double target_ud_ratio = 1.65;
    for (size_t idx : strange) {
        const double ratio = static_cast<double>(up_like.size() + 1) / static_cast<double>(down_like.size() + 1);
        if (ratio < target_ud_ratio) up_like.push_back(idx);
        else                         down_like.push_back(idx);
    }

    const int baryon_budget = static_cast<int>((up_like.size() + down_like.size()) / 3);
    const int hadronization_budget = std::min(baryon_budget, static_cast<int>(gluons.size()));
    const int target_neutrons = static_cast<int>(std::lround(hadronization_budget * NuclearAbundances::DEFAULT_Xn));
    const int min_neutrons = std::max(0, 2 * hadronization_budget - static_cast<int>(up_like.size()));
    const int max_neutrons = std::min(hadronization_budget, static_cast<int>(down_like.size()) - hadronization_budget);
    const int neutron_quota = std::clamp(target_neutrons, min_neutrons, std::max(min_neutrons, max_neutrons));
    const int proton_quota = std::max(0, hadronization_budget - neutron_quota);

    std::vector<uint8_t> used(n, 0);
    const std::array<double, 4> radius_passes = {
        binding_radius,
        binding_radius * 2.0,
        binding_radius * 4.0,
        std::numeric_limits<double>::infinity()
    };

    auto formBaryons = [&](ParticleType type,
                           const std::vector<size_t>& primary,
                           const std::vector<size_t>& same_kind,
                           const std::vector<size_t>& other_kind,
                           int quota,
                           size_t& formed_counter)
    {
        for (double radius : radius_passes) {
            while (static_cast<int>(formed_counter) < quota) {
                const TripletCandidate candidate = findBestTriplet(pool, primary, same_kind, other_kind, gluons, used, radius);
                if (!candidate.valid || candidate.gluon == kInvalidIndex) break;

                const double cx = (pool.x[candidate.first] + pool.x[candidate.second] + pool.x[candidate.third]) / 3.0;
                const double cy = (pool.y[candidate.first] + pool.y[candidate.second] + pool.y[candidate.third]) / 3.0;
                const double cz = (pool.z[candidate.first] + pool.z[candidate.second] + pool.z[candidate.third]) / 3.0;
                const double cvx = (pool.vx[candidate.first] + pool.vx[candidate.second] + pool.vx[candidate.third]) / 3.0;
                const double cvy = (pool.vy[candidate.first] + pool.vy[candidate.second] + pool.vy[candidate.third]) / 3.0;
                const double cvz = (pool.vz[candidate.first] + pool.vz[candidate.second] + pool.vz[candidate.third]) / 3.0;

                used[candidate.first] = used[candidate.second] = used[candidate.third] = 1;
                used[candidate.gluon] = 1;
                pool.deactivate(candidate.first);
                pool.deactivate(candidate.second);
                pool.deactivate(candidate.third);
                pool.deactivate(candidate.gluon);
                ++stats.confined_gluons;

                float r, g, b;
                ParticlePool::defaultColor(type, r, g, b);
                const size_t added = pool.add(cx, cy, cz, cvx, cvy, cvz, restMass(type), type, r, g, b,
                                              static_cast<float>(atomicCharge(type)));
                applyHadronProperties(pool, added, type, candidate.gluon != kInvalidIndex);
                ++formed_counter;
            }
        }
    };

    formBaryons(ParticleType::PROTON, up_like, up_like, down_like, proton_quota, stats.formed_protons);
    formBaryons(ParticleType::NEUTRON, down_like, down_like, up_like, neutron_quota, stats.formed_neutrons);

    for (size_t i = 0; i < n; ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        if (pool.type[i] == ParticleType::QUARK_U || pool.type[i] == ParticleType::QUARK_D || pool.type[i] == ParticleType::QUARK_S) {
            pool.deactivate(i);
            ++stats.residual_quarks;
        }
    }
    for (size_t idx : gluons) {
        if (pool.flags[idx] & PF_ACTIVE) {
            pool.deactivate(idx);
            ++stats.confined_gluons;
        }
    }

    return stats;
}

NuclearAbundances inferAbundances(const ParticlePool& pool) {
    NuclearAbundances ab{};
    ab.Xn = ab.Xp = ab.Xd = ab.Xhe3 = ab.Xhe4 = ab.Xli7 = 0.0;

    double total_baryons = 0.0;
    for (size_t i = 0; i < pool.x.size(); ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        const double baryons = static_cast<double>(baryonNumber(pool.type[i]));
        if (baryons <= 0.0) continue;
        total_baryons += baryons;
        switch (pool.type[i]) {
            case ParticleType::PROTON:     ab.Xp   += baryons; break;
            case ParticleType::NEUTRON:    ab.Xn   += baryons; break;
            case ParticleType::DEUTERIUM:  ab.Xd   += baryons; break;
            case ParticleType::HELIUM3:    ab.Xhe3 += baryons; break;
            case ParticleType::HELIUM4NUCLEI: ab.Xhe4 += baryons; break;
            case ParticleType::LITHIUM7:   ab.Xli7 += baryons; break;
            default: break;
        }
    }

    if (total_baryons <= 0.0) {
        ab.Xp = NuclearAbundances::DEFAULT_Xp;
        ab.Xn = NuclearAbundances::DEFAULT_Xn;
        return ab;
    }

    const double inv = 1.0 / total_baryons;
    ab.Xp   *= inv;
    ab.Xn   *= inv;
    ab.Xd   *= inv;
    ab.Xhe3 *= inv;
    ab.Xhe4 *= inv;
    ab.Xli7 *= inv;
    return ab;
}

} // namespace chemistry
