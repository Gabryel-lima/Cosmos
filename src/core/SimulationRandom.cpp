#include "SimulationRandom.hpp"

#include "RegimeConfig.hpp"

namespace simrng {
namespace {

std::uint32_t g_seed = RegimeConfig::DEFAULT_RANDOM_SEED;

std::uint32_t hashStream(std::string_view stream_name) {
    std::uint32_t hash = 2166136261u;
    for (char c : stream_name) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

} // namespace

void setGlobalSeed(std::uint32_t seed) {
    g_seed = (seed == 0u) ? RegimeConfig::DEFAULT_RANDOM_SEED : seed;
}

std::uint32_t globalSeed() {
    return g_seed;
}

std::mt19937 makeStream(std::string_view stream_name) {
    const std::uint32_t mixed = g_seed ^ (hashStream(stream_name) + 0x9e3779b9u + (g_seed << 6) + (g_seed >> 2));
    return std::mt19937(mixed);
}

} // namespace simrng
