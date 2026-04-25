#pragma once

#include <cstdint>
#include <random>
#include <string_view>

namespace simrng {

void setGlobalSeed(std::uint32_t seed);
std::uint32_t globalSeed();
std::mt19937 makeStream(std::string_view stream_name);

} // namespace simrng
