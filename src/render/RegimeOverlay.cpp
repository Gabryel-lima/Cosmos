// src/render/RegimeOverlay.cpp — Implementação do HUD Dear ImGui.
#include "RegimeOverlay.hpp"
#include "../core/Camera.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/RegimeManager.hpp"
#include "../core/Universe.hpp"
#include "../physics/Constants.hpp"
#include "../physics/Friedmann.hpp"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <vector>

static const char* REGIME_NAMES[CosmicClock::REGIME_COUNT] = {
    "0:INFLATION", "1:QGP", "2:BBN", "3:PLASMA", "4:DARK AGES", "5:REIONIZATION", "6:STRUCTURE"
};

static const char* SPEED_LABELS[] = {
    "x0.01 SLOW", "NORMAL", "x2", "x10", "x100"
};

static ImVec4 particleColor(ParticleType type, float alpha = 1.0f) {
    float r, g, b;
    ParticlePool::defaultColor(type, r, g, b);
    return {r, g, b, alpha};
}

// Formata um número em notação científica (ex: 1.23 x 10^8)
static void fmtSci(char* buf, size_t len, double v) {
    if (v == 0.0) { snprintf(buf, len, "0"); return; }
    int exp = static_cast<int>(std::floor(std::log10(std::abs(v))));
    double mantissa = v / std::pow(10.0, exp);
    snprintf(buf, len, "%.3g x 10^%d", mantissa, exp);
}

void RegimeOverlay::render(CosmicClock& clock, RegimeManager& mgr, Universe& universe, Camera& camera) {
    if (!visible) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({static_cast<float>(io.DisplaySize.x), 200.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove   | ImGuiWindowFlags_NoScrollbar;

    if (ImGui::Begin("##CosmicHUD", nullptr, flags)) {
        drawTimeline(clock, mgr);
        ImGui::Separator();
        drawTimeControls(clock, mgr, universe, camera);
        ImGui::SameLine();
        drawPhysicsInfo(clock, universe);
    }
    ImGui::End();

    // Painel de composição (visível em todos os regimes)
    ImGui::SetNextWindowPos({10, 210}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({300, 220}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.72f);
    if (ImGui::Begin("Cosmic Composition", nullptr, ImGuiWindowFlags_NoCollapse)) {
        drawCompositionTable(clock, universe);
    }
    ImGui::End();

    // Overlay de desempenho (canto superior direito)
    ImGui::SetNextWindowPos({io.DisplaySize.x - 245.0f, 210.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({226, 96}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("##Perf", nullptr, flags)) {
        drawPerformanceStats(universe);
    }
    ImGui::End();
}

void RegimeOverlay::drawTimeline(CosmicClock& clock, RegimeManager& mgr) {
    float total_w = ImGui::GetContentRegionAvail().x;

    // Desenhar segmentos de regime
    const ImVec4 regime_colors[CosmicClock::REGIME_COUNT] = {
        {0.88f, 0.95f, 0.56f, 1.0f},
        {0.96f, 0.48f, 0.18f, 1.0f},
        {0.58f, 0.36f, 0.98f, 1.0f},
        {1.00f, 0.70f, 0.28f, 1.0f},
        {0.22f, 0.48f, 0.78f, 1.0f},
        {0.20f, 0.82f, 0.90f, 1.0f},
        {0.54f, 0.24f, 0.82f, 1.0f}
    };

    float x0 = ImGui::GetCursorScreenPos().x;
    float y0 = ImGui::GetCursorScreenPos().y;
    float h  = 18.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float segment_gap = 1.0f;
    const float segment_w = std::max((total_w - segment_gap * static_cast<float>(CosmicClock::REGIME_COUNT - 1)) /
                                     static_cast<float>(CosmicClock::REGIME_COUNT),
                                     1.0f);

    for (int i = 0; i < CosmicClock::REGIME_COUNT; ++i) {
        float seg_start = x0 + static_cast<float>(i) * (segment_w + segment_gap);
        float seg_end = seg_start + segment_w;
        ImVec4 c = regime_colors[i];
        if (i == clock.getCurrentRegimeIndex()) {
            c.x = std::min(1.0f, c.x * 1.4f);
            c.y = std::min(1.0f, c.y * 1.4f);
            c.z = std::min(1.0f, c.z * 1.4f);
        }
        dl->AddRectFilled({seg_start, y0}, {seg_end, y0+h},
                          ImGui::ColorConvertFloat4ToU32(c), 2.0f);
        dl->AddRect({seg_start, y0}, {seg_end, y0+h}, IM_COL32(18, 18, 26, 220), 2.0f, 0, 1.0f);

        float brightness = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
        ImU32 text_color = (brightness > 0.72f) ? IM_COL32(25, 25, 35, 255)
                                                : IM_COL32_WHITE;
        const char* label = REGIME_NAMES[i];
        ImVec2 text_size = ImGui::CalcTextSize(label);
        float text_x = seg_start + std::max((segment_w - text_size.x) * 0.5f, 4.0f);
        float text_y = y0 + std::max((h - text_size.y) * 0.5f, 1.0f);
        dl->PushClipRect({seg_start + 2.0f, y0}, {seg_end - 2.0f, y0 + h}, true);
        dl->AddText({text_x, text_y}, text_color, label);
        dl->PopClipRect();
    }

    // Durante o blend automático, mantenha o marcador travado no limiar do novo regime.
    // Isso evita a impressão de que a transição começou "dentro" do segmento seguinte.
    // Marcador do momento atual (posição do cursor de scrubbing)
    float marker_x = x0;
    const int current_regime = std::clamp(clock.getCurrentRegimeIndex(), 0, CosmicClock::LAST_REGIME_INDEX);
    const float regime_progress = (mgr.getTransitionProgress() > 0.0f)
        ? 0.0f
        : static_cast<float>(std::clamp(clock.getRegimeProgress(), 0.0, 1.0));
    marker_x += static_cast<float>(current_regime) * (segment_w + segment_gap);
    marker_x += regime_progress * segment_w;
    dl->AddLine({marker_x, y0}, {marker_x, y0+h+4}, IM_COL32(255,255,0,255), 2.0f);
    dl->AddTriangleFilled({marker_x, y0+h+4},
                          {marker_x-5, y0+h+12},
                          {marker_x+5, y0+h+12},
                          IM_COL32(255,255,0,200));
    ImGui::Dummy({total_w, h+14});
}

void RegimeOverlay::drawTimeControls(CosmicClock& clock, RegimeManager& mgr, Universe& universe, Camera& camera) {
    ImGui::BeginGroup();

    // Reproduzir/Pausar
    if (clock.isPaused()) {
        if (ImGui::Button("> PLAY")) clock.play();
    } else {
        if (ImGui::Button("|| PAUSE")) clock.pause();
    }
    ImGui::SameLine();
    if (ImGui::Button("|>> STEP")) clock.stepSingleFrame();
    ImGui::SameLine();
    if (ImGui::Button("<< PREV REGIME")) {
        int rewind_regime = std::max(0, clock.getCurrentRegimeIndex() - 1);
        mgr.jumpToRegime(rewind_regime, clock, universe);
        camera.applyState(camera.getRegimeDefaultState(rewind_regime));
    }

    // Predefinições de velocidade
    ImGui::SameLine();
    if (ImGui::Button("SPEED")) {
        speed_preset_index_ = (speed_preset_index_ + 1) % 5;
        clock.applySpeedPreset(static_cast<CosmicClock::SpeedPreset>(speed_preset_index_));
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(SPEED_LABELS[speed_preset_index_]);

    // Controle deslizante de escala de tempo (logarítmico)
    float log_scale = std::log10(static_cast<float>(clock.getTimeScale()) + 1e-45f);
    ImGui::PushItemWidth(200.0f);
    if (ImGui::SliderFloat("Time scale (log)", &log_scale, -40.0f, 20.0f)) {
        clock.setTimeScale(std::pow(10.0, static_cast<double>(log_scale)));
    }
    ImGui::PopItemWidth();

    // Botões de salto
    ImGui::Text("Jump to:");
    for (int i = 0; i < CosmicClock::REGIME_COUNT; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::SmallButton(REGIME_NAMES[i])) {
            mgr.jumpToRegime(i, clock, universe);
            camera.applyState(camera.getRegimeDefaultState(i));
        }
    }

    ImGui::EndGroup();
}

void RegimeOverlay::drawPhysicsInfo(const CosmicClock& clock, const Universe& /*universe*/) {
    ImGui::BeginGroup();
    char buf[64];
    double t = clock.getCosmicTime();
    if (t >= phys::yr_to_s) {
        double t_yr = t / phys::yr_to_s;
        if (t_yr >= 1e9)
            snprintf(buf, sizeof(buf), "t = %.3g Gyr", t_yr * 1e-9);
        else if (t_yr >= 1e6)
            snprintf(buf, sizeof(buf), "t = %.3g Myr", t_yr * 1e-6);
        else if (t_yr >= 1.0)
            snprintf(buf, sizeof(buf), "t = %.3g yr",  t_yr);
        else
            snprintf(buf, sizeof(buf), "t = %.3g s",   t);
    } else {
        fmtSci(buf, sizeof(buf), t); // raw seconds
        ImGui::Text("t = %s s", buf);
        buf[0] = '\0';
    }
    if (buf[0] != '\0') {
        ImGui::TextUnformatted(buf);
    }

    char Tbuf[64]; fmtSci(Tbuf, sizeof(Tbuf), clock.getTemperatureKeV());
    ImGui::Text("T = %s keV", Tbuf);
    ImGui::Text("a = %.5g", clock.getScaleFactor());
    char Hbuf[64]; fmtSci(Hbuf, sizeof(Hbuf), clock.getHubbleRate());
    ImGui::Text("H = %s s⁻¹", Hbuf);
    ImGui::Text("Regime: %s", REGIME_NAMES[clock.getCurrentRegimeIndex()]);
    ImGui::EndGroup();
}

void RegimeOverlay::drawCompositionTable(const CosmicClock& clock, const Universe& universe) {
    int regime = clock.getCurrentRegimeIndex();
    
    // Auxiliar progress bar
    auto bar = [&](const char* label, double val, ImVec4 color) {
        ImGui::TextColored(color, "%-10s", label);
        ImGui::SameLine(110);
        char buf[32]; snprintf(buf, sizeof(buf), "%.2f%%", val * 100.0);
        ImGui::ProgressBar(static_cast<float>(val), {155, 14}, buf);
    };

    if (regime == 0) {
        ImGui::Text("Inflation Era");
        ImGui::Text("Scalar Field (Inflaton) Vacuum Energy");
        bar("Inflaton", 1.0, {0.2f, 0.4f, 1.0f, 1.0f});
        return;
    }

    if (regime == 2) {
        // Para BBN, usamos as frações exatas do NuclearNetwork
        ImGui::Text("Nuclear Abundances (Mass Fraction)");
        const NuclearAbundances& ab = universe.abundances;
        bar("Proton (H)",  ab.Xp,   particleColor(ParticleType::PROTON));
        bar("Neutron",     ab.Xn,   particleColor(ParticleType::NEUTRON));
        bar("Deuterium",   ab.Xd,   particleColor(ParticleType::DEUTERIUM));
        bar("Helium-3",    ab.Xhe3, particleColor(ParticleType::HELIUM3));
        bar("He-4 Nuclei", ab.Xhe4, particleColor(ParticleType::HELIUM4NUCLEI));
        bar("Lithium-7",   ab.Xli7, particleColor(ParticleType::LITHIUM7));
        return;
    }

    // Para outros regimes, contamos ativamente as partículas simuladas
    std::unordered_map<ParticleType, int> counts;
    int total = 0;
    const ParticlePool& pp = universe.particles;
    for (size_t i = 0; i < pp.x.size(); ++i) {
        if (!(pp.flags[i] & PF_ACTIVE)) continue;
        counts[pp.type[i]]++;
        total++;
    }

    if (total == 0) {
        ImGui::Text("Awaiting Particle Generation...");
        return;
    }

    ImGui::Text("Particle Distribution (%d total)", total);
    
    // Função local para mapear tipo -> cor & nome
    auto renderType = [&](ParticleType type, const char* name, ImVec4 color) {
        if (counts[type] > 0) {
            double frac = static_cast<double>(counts[type]) / total;
            bar(name, frac, color);
        }
    };

    if (regime == 1) {
        renderType(ParticleType::QUARK_U,       "Quark Up",      particleColor(ParticleType::QUARK_U));
        renderType(ParticleType::QUARK_D,       "Quark Down",    particleColor(ParticleType::QUARK_D));
        renderType(ParticleType::QUARK_S,       "Quark Strange", particleColor(ParticleType::QUARK_S));
        renderType(ParticleType::GLUON,         "Gluon",         particleColor(ParticleType::GLUON));
        renderType(ParticleType::PROTON,        "Protons",       particleColor(ParticleType::PROTON));
        renderType(ParticleType::NEUTRON,       "Neutrons",      particleColor(ParticleType::NEUTRON));
    } else if (regime == 3) {
        renderType(ParticleType::DEUTERIUM,     "Deuterium",     particleColor(ParticleType::DEUTERIUM));
        renderType(ParticleType::HELIUM3,       "Helium-3",      particleColor(ParticleType::HELIUM3));
        renderType(ParticleType::HELIUM4NUCLEI, "He-4 Nuclei",   particleColor(ParticleType::HELIUM4NUCLEI));
        renderType(ParticleType::LITHIUM7,      "Lithium-7",     particleColor(ParticleType::LITHIUM7));
        renderType(ParticleType::PHOTON,        "Photons",       particleColor(ParticleType::PHOTON));
        renderType(ParticleType::PROTON,        "Protons",       particleColor(ParticleType::PROTON));
        renderType(ParticleType::ELECTRON,      "Electrons",     particleColor(ParticleType::ELECTRON));
        renderType(ParticleType::NEUTRINO,      "Neutrinos",     particleColor(ParticleType::NEUTRINO));
    } else if (regime == 4) {
        renderType(ParticleType::DARK_MATTER,   "Dark Matter",   particleColor(ParticleType::DARK_MATTER));
        renderType(ParticleType::GAS,           "Neutral Gas",   particleColor(ParticleType::GAS));
        renderType(ParticleType::PHOTON,        "CMB Photons",   particleColor(ParticleType::PHOTON));
    } else if (regime == 5) {
        renderType(ParticleType::DARK_MATTER,   "Dark Matter",   particleColor(ParticleType::DARK_MATTER));
        renderType(ParticleType::GAS,           "Ionized Gas",   particleColor(ParticleType::GAS));
        renderType(ParticleType::STAR,          "First Stars",   particleColor(ParticleType::STAR));
        renderType(ParticleType::BLACKHOLE,     "Seed BH",       particleColor(ParticleType::BLACKHOLE));
    } else if (regime == 6) {
        renderType(ParticleType::DARK_MATTER,   "Dark Matter",   particleColor(ParticleType::DARK_MATTER));
        renderType(ParticleType::GAS,           "Gas",           particleColor(ParticleType::GAS));
        renderType(ParticleType::STAR,          "Stars",         particleColor(ParticleType::STAR));
        renderType(ParticleType::BLACKHOLE,     "Black Holes",   particleColor(ParticleType::BLACKHOLE));
    }
}

void RegimeOverlay::drawPerformanceStats(const Universe& universe) {
    ImGui::Text("FPS: %.1f", universe.fps);
    ImGui::Text("GPU: %.2f ms", universe.gpu_time_ms);

    // Contar partículas ativas por tipo (mesma lógica da tabela de composição)
    std::unordered_map<ParticleType, int> counts;
    const ParticlePool& pp = universe.particles;
    int total_active = 0;
    for (size_t i = 0; i < pp.x.size(); ++i) {
        if (!(pp.flags[i] & PF_ACTIVE)) continue;
        counts[pp.type[i]]++;
        total_active++;
    }

    // Mostrar informações específicas por regime, usando contagens reais
    if (universe.regime_index == 0) {
        ImGui::Text("Inflation field: %dx%d", universe.phi_NX, universe.phi_NY);
        ImGui::Text("Phi samples: %zu", universe.phi_field.size());
        ImGui::Text("Active particles: %d", total_active);
    } else if (universe.regime_index == 1) {
        int qu_u = counts[ParticleType::QUARK_U];
        int qu_d = counts[ParticleType::QUARK_D];
        int qu_s = counts[ParticleType::QUARK_S];
        int gluons_est = (qu_u + qu_d + qu_s) / std::max(1, RegimeConfig::QGP_GLUON_RATIO_DIVISOR);
        ImGui::Text("Active particles: %d", total_active);
        ImGui::Text("Quarks U/D/S: %d/%d/%d", qu_u, qu_d, qu_s);
        ImGui::Text("Estimated Gluons: %d", gluons_est);
    } else if (universe.regime_index == 2) {
        int protons = counts[ParticleType::PROTON];
        int neutrons = counts[ParticleType::NEUTRON];
        ImGui::Text("Active particles: %d", total_active);
        ImGui::Text("Protons: %d Neutrons: %d", protons, neutrons);
        ImGui::Text("Abundances Xp: %.3f Xn: %.3f", universe.abundances.Xp, universe.abundances.Xn);
    } else if (universe.regime_index == 3) {
        ImGui::Text("Active particles: %d", total_active);
        ImGui::Text("Photons: %d", counts[ParticleType::PHOTON]);
        ImGui::Text("Protons: %d Electrons: %d", counts[ParticleType::PROTON], counts[ParticleType::ELECTRON]);
    } else if (universe.regime_index == 4) {
        ImGui::Text("Active particles: %d", total_active);
        ImGui::Text("Dark Matter: %d", counts[ParticleType::DARK_MATTER]);
        ImGui::Text("Neutral Gas: %d", counts[ParticleType::GAS]);
    } else if (universe.regime_index == 5) {
        ImGui::Text("Active particles: %d", total_active);
        ImGui::Text("First Stars: %d", counts[ParticleType::STAR]);
        ImGui::Text("Ionized Gas: %d", counts[ParticleType::GAS]);
    } else if (universe.regime_index == 6) {
        ImGui::Text("Active particles: %d", total_active);
        ImGui::Text("Dark Matter: %d Gas: %d", counts[ParticleType::DARK_MATTER], counts[ParticleType::GAS]);
        ImGui::Text("Stars: %d Black Holes: %d", counts[ParticleType::STAR], counts[ParticleType::BLACKHOLE]);
    } else {
        ImGui::Text("Active particles: %d", total_active);
    }
}
