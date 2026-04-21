#pragma once
// CpuFeatures.h — detecção de ISA em runtime via __builtin_cpu_supports
// Seguro chamar antes de qualquer código AVX; o resultado é constante por
// processo e pode ser armazenado em cache com std::call_once se necessário.

/* @note: Esse carinha é novo aqui */

namespace cpu {

/// Retorna true se a CPU suportar AVX2 + FMA3 (Haswell ou posterior).
/// A verificação é feita uma única vez e cacheada em uma variável estática.
inline bool has_avx2() noexcept {
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
    // __builtin_cpu_supports emite CPUID inline; nunca lança exceção.
    static const bool result =
        __builtin_cpu_supports("avx2") &&
        __builtin_cpu_supports("fma");
    return result;
#else
    return false;   // MSVC ou arquitetura não-x86: fallback seguro
#endif
}

/// Retorna true se a CPU suportar pelo menos AVX1 (Sandy Bridge ou posterior).
inline bool has_avx() noexcept {
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
    static const bool result = __builtin_cpu_supports("avx");
    return result;
#else
    return false;
#endif
}

} // namespace cpu
