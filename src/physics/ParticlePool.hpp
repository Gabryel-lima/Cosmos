#pragma once
// src/physics/ParticlePool.hpp — Array plano de partículas SOA, amigável ao cache.
// Todos os dados de partículas residem aqui. Regimes manipulam isso diretamente.

#include "QcdColor.hpp"
#include <cstdint>
#include <vector>

/// Enumeração de tipos de partícula.
enum class ParticleType : uint8_t {
    QUARK_U       = 0,
    QUARK_D       = 1,
    QUARK_S       = 2,
    QUARK_C       = 3,
    QUARK_B       = 4,
    QUARK_T       = 5,
    ANTIQUARK_U   = 6,
    ANTIQUARK_D   = 7,
    ANTIQUARK_S   = 8,
    ANTIQUARK_C   = 9,
    ANTIQUARK_B   = 10,
    ANTIQUARK_T   = 11,
    ANTIQUARK     = 12,
    GLUON         = 13,
    PROTON        = 14,
    NEUTRON       = 15,
    ELECTRON      = 16,
    POSITRON      = 17,
    MUON          = 18,
    ANTIMUON      = 19,
    TAU           = 20,
    ANTITAU       = 21,
    PHOTON        = 22,
    NEUTRINO      = 23,
    NEUTRINO_E    = 24,
    ANTINEUTRINO_E= 25,
    NEUTRINO_MU   = 26,
    ANTINEUTRINO_MU = 27,
    NEUTRINO_TAU  = 28,
    ANTINEUTRINO_TAU = 29,
    W_BOSON_POS   = 30,
    W_BOSON_NEG   = 31,
    Z_BOSON       = 32,
    HIGGS_BOSON   = 33,
    DEUTERIUM     = 34,
    HELIUM3       = 35,
    HELIUM4NUCLEI = 36,
    LITHIUM7      = 37,
    DARK_MATTER   = 38,
    STAR          = 39,
    GAS           = 40,
    BLACKHOLE     = 41,
    COUNT
};

/// Máscara de bits de flags da partícula.
enum ParticleFlags : uint32_t {
    PF_ACTIVE       = 1u << 0,
    PF_BOUND        = 1u << 1,
    PF_ANNIHILATED  = 1u << 2,
    PF_COLLAPSING   = 1u << 3,
    PF_STAR_FORMED  = 1u << 4,
};

/// Estado do ciclo de vida de partículas estelares.
enum class StarState : uint8_t {
    NONE = 0,
    PROTOSTAR,
    MAIN_SEQUENCE,
    SUBGIANT,
    RED_GIANT,
    BLUE_GIANT,
    WHITE_DWARF,
    NEUTRON_STAR,
    BLACK_HOLE,
};

/// Contêner de partículas Estrutura-de-Arrays.
/// Todos os arrays são mantidos sincronizados — índice i refere-se à mesma partícula em todos os arrays.
struct ParticlePool {
    // Posição (espaço do mundo, dupla precisão)
    std::vector<double> x, y, z;
    // Velocidade [m/s]
    std::vector<double> vx, vy, vz;
    // Massa [kg]
    std::vector<double> mass;
    // Tipo de partícula
    std::vector<ParticleType> type;
    // Carga elétrica [C] (com sinal)
    std::vector<float> charge;
    // Cor visual (0..1 RGB)
    std::vector<float> color_r, color_g, color_b;
    // Estado de cor QCD simplificado
    std::vector<QcdColor> qcd_color, qcd_anticolor;
    // Luminosidade (para estrelas, usada para renderizar brilho)
    std::vector<float> luminosity;
    // Temperatura da partícula [K] (individual, para partículas estelares)
    std::vector<float> temp_particle;
    // Estado do ciclo de vida estelar
    std::vector<StarState> star_state;
    // Idade da estrela [s]
    std::vector<double> star_age;
    // Máscara de bits de flags
    std::vector<uint32_t> flags;

    // Capacidade total / contagem ativa
    size_t capacity = 0;
    size_t active   = 0;

    void resize(size_t n);
    void clear();

    /// Adicionar uma partícula; retorna seu índice. Define a flag PF_ACTIVE.
    size_t add(double px, double py, double pz,
               double pvx, double pvy, double pvz,
               double mass_kg, ParticleType t,
               float cr, float cg, float cb,
               float charge_val = 0.0f);

    /// Desativar uma partícula (definir flag para ~PF_ACTIVE).
    void deactivate(size_t i);

    /// Compactar: remover partículas desativadas (encolhe os arrays).
    void compact();

    /// Obter cor visual para um tipo de partícula (cores padrão).
    static void defaultColor(ParticleType t, float& r, float& g, float& b);

    /// Ajustar o estado cromático QCD e atualizar a cor visual correspondente.
    void setQcdCharge(size_t i, QcdColor color, QcdColor anticolor = QcdColor::NONE);
    void clearQcdCharge(size_t i);
    static void applyQcdTint(ParticleType t, QcdColor color, QcdColor anticolor,
                             float& r, float& g, float& b);

    /// Escala visual relativa do sprite para cada tipo/estado de partícula.
    static float defaultVisualScale(ParticleType t, uint32_t flags = 0u);
};
