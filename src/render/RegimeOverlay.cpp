// src/render/RegimeOverlay.cpp — Implementação do HUD Dear ImGui.
#include "RegimeOverlay.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/RegimeManager.hpp"
#include "../core/Universe.hpp"
#include "../physics/Constants.hpp"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

static const char* REGIME_NAMES[5] = {
    "0:INFLATION", "1:QGP", "2:BBN", "3:PLASMA", "4:STRUCTURE"
};

static const char* SPEED_LABELS[] = {
    "×0.01 SLOW", "NORMAL", "×2", "×10", "×100"
};

// Formata um número em notação científica (ex: 1.23 × 10⁸)
static void fmtSci(char* buf, size_t len, double v) {
    if (v == 0.0) { snprintf(buf, len, "0"); return; }
    int exp = static_cast<int>(std::floor(std::log10(std::abs(v))));
    double mantissa = v / std::pow(10.0, exp);
    snprintf(buf, len, "%.3g × 10^%d", mantissa, exp);
}

void RegimeOverlay::render(CosmicClock& clock, RegimeManager& mgr, Universe& universe) {
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
        drawTimeControls(clock, mgr, universe);
        ImGui::SameLine();
        drawPhysicsInfo(clock, universe);
    }
    ImGui::End();

    // Painel de abundâncias (visível apenas no Regime 2)
    if (clock.getCurrentRegimeIndex() == 2) {
        ImGui::SetNextWindowPos({10, 210}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({260, 160}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);
        if (ImGui::Begin("##Abundances", nullptr, flags)) {
            drawAbundancePieChart(universe);
        }
        ImGui::End();
    }

    // Overlay de desempenho (canto superior direito)
    ImGui::SetNextWindowPos({io.DisplaySize.x - 200.0f, 210.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({190, 90}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("##Perf", nullptr, flags)) {
        drawPerformanceStats(universe);
    }
    ImGui::End();
}

void RegimeOverlay::drawTimeline(CosmicClock& clock, RegimeManager& /*mgr*/) {
    float total_w = ImGui::GetContentRegionAvail().x;

    // Desenhar segmentos de regime
    const float regime_widths[5] = {0.04f, 0.06f, 0.05f, 0.10f, 0.75f};
    const ImVec4 regime_colors[5] = {
        {1.0f, 1.0f, 0.8f, 1.0f}, {1.0f, 0.4f, 0.1f, 1.0f},
        {0.4f, 0.2f, 0.8f, 1.0f}, {1.0f, 0.6f, 0.2f, 1.0f},
        {0.1f, 0.2f, 0.5f, 1.0f}
    };

    float x0 = ImGui::GetCursorScreenPos().x;
    float y0 = ImGui::GetCursorScreenPos().y;
    float h  = 18.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = x0;
    for (int i = 0; i < 5; ++i) {
        float w = total_w * regime_widths[i];
        ImVec4 c = regime_colors[i];
        if (i == clock.getCurrentRegimeIndex()) {
            c.x = std::min(1.0f, c.x * 1.4f);
            c.y = std::min(1.0f, c.y * 1.4f);
            c.z = std::min(1.0f, c.z * 1.4f);
        }
        dl->AddRectFilled({x, y0}, {x+w, y0+h},
                          ImGui::ColorConvertFloat4ToU32(c));
        dl->AddText({x+4, y0+2}, IM_COL32_WHITE, REGIME_NAMES[i]);
        x += w;
    }

    // Marcador do momento atual (posição do cursor de scrubbing)
    // escala logarítmica: 0=10^-43, 1=hoje
    double log_min = -43.0, log_max = std::log10(phys::t_today);
    double log_t   = std::log10(std::max(clock.getCosmicTime(), 1e-43));
    float  pos_f   = static_cast<float>((log_t - log_min) / (log_max - log_min));
    pos_f = std::clamp(pos_f, 0.0f, 1.0f);
    float marker_x = x0 + pos_f * total_w;
    dl->AddLine({marker_x, y0}, {marker_x, y0+h+4}, IM_COL32(255,255,0,255), 2.0f);
    dl->AddTriangleFilled({marker_x, y0+h+4},
                          {marker_x-5, y0+h+12},
                          {marker_x+5, y0+h+12},
                          IM_COL32(255,255,0,200));
    ImGui::Dummy({total_w, h+14});
}

void RegimeOverlay::drawTimeControls(CosmicClock& clock, RegimeManager& mgr, Universe& universe) {
    ImGui::BeginGroup();

    // Reproduzir/Pausar
    if (clock.isPaused()) {
        if (ImGui::Button("▶ PLAY")) clock.play();
    } else {
        if (ImGui::Button("⏸ PAUSE")) clock.pause();
    }
    ImGui::SameLine();
    if (ImGui::Button("⏮ STEP")) clock.stepSingleFrame();

    // Predefinições de velocidade
    ImGui::SameLine();
    if (ImGui::Button("S: SPEED")) {
        speed_preset_index_ = (speed_preset_index_ + 1) % 5;
        clock.applySpeedPreset(static_cast<CosmicClock::SpeedPreset>(speed_preset_index_));
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(SPEED_LABELS[speed_preset_index_]);

    // Controle deslizante de escala de tempo (logarítmico)
    float log_scale = std::log10(static_cast<float>(clock.getTimeScale()) + 1e-50f);
    ImGui::PushItemWidth(200.0f);
    if (ImGui::SliderFloat("Time scale (log)", &log_scale, -40.0f, 20.0f)) {
        clock.setTimeScale(std::pow(10.0, static_cast<double>(log_scale)));
    }
    ImGui::PopItemWidth();

    // Botões de salto
    ImGui::Text("Jump to:");
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::SmallButton(REGIME_NAMES[i])) {
            mgr.jumpToRegime(i, clock, universe);
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
        char tmp[80]; snprintf(tmp, sizeof(tmp), "t = %s s", buf);
        strncpy(buf, tmp, sizeof(buf));
    }
    ImGui::TextUnformatted(buf);

    char Tbuf[64]; fmtSci(Tbuf, sizeof(Tbuf), clock.getTemperatureKeV());
    ImGui::Text("T = %s keV", Tbuf);
    ImGui::Text("a = %.5g", clock.getScaleFactor());
    char Hbuf[64]; fmtSci(Hbuf, sizeof(Hbuf), clock.getHubbleRate());
    ImGui::Text("H = %s s⁻¹", Hbuf);
    ImGui::Text("Regime: %s", REGIME_NAMES[clock.getCurrentRegimeIndex()]);
    ImGui::EndGroup();
}

void RegimeOverlay::drawAbundancePieChart(const Universe& universe) {
    ImGui::Text("Nuclear Abundances (BBN)");
    const NuclearAbundances& ab = universe.abundances;
    auto bar = [&](const char* label, double val, ImVec4 color) {
        ImGui::TextColored(color, "%s", label);
        ImGui::SameLine(80);
        char buf[32]; snprintf(buf, sizeof(buf), "%.4f%%", val * 100.0);
        ImGui::ProgressBar(static_cast<float>(val), {120, 14}, buf);
    };
    bar("n",    ab.Xn,   {0.3f,0.3f,1.0f,1.0f});
    bar("p",    ab.Xp,   {1.0f,0.2f,0.2f,1.0f});
    bar("D",    ab.Xd,   {0.7f,0.0f,1.0f,1.0f});
    bar("He3",  ab.Xhe3, {1.0f,0.5f,0.1f,1.0f});
    bar("He4",  ab.Xhe4, {1.0f,0.7f,0.0f,1.0f});
    bar("Li7",  ab.Xli7, {0.0f,1.0f,0.4f,1.0f});
}

void RegimeOverlay::drawPerformanceStats(const Universe& universe) {
    ImGui::Text("FPS:     %.1f", universe.fps);
    ImGui::Text("GPU:     %.2f ms", universe.gpu_time_ms);
    ImGui::Text("Particles: %d", universe.active_particles);
}
