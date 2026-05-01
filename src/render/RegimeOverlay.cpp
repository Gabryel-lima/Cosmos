// src/render/RegimeOverlay.cpp — Implementação do HUD Dear ImGui.
#include "RegimeOverlay.hpp"
#include "../core/Camera.hpp"
#include "../core/CosmicClock.hpp"
#include "../core/RegimeManager.hpp"
#include "../core/Universe.hpp"
#include "../physics/Constants.hpp"
#include "../physics/Friedmann.hpp"
#include "../physics/QcdColor.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <vector>

static const char* REGIME_NAMES[CosmicClock::REGIME_COUNT] = {
    "0:INFLATION", "1:REHEAT", "2:LEPTON", "3:QGP", "4:BBN", "5:PLASMA", "6:DARK AGES", "7:REIONIZATION", "8:STRUCTURE"
};

static const char* SPEED_LABELS[] = {
    "x0.01 SLOW", "NORMAL", "x2", "x10", "x100"
};

static const char* speedPresetLabel(int preset_index) {
    if (preset_index < 0 || preset_index >= static_cast<int>(std::size(SPEED_LABELS))) {
        return "CUSTOM";
    }
    return SPEED_LABELS[preset_index];
}

static ImVec4 particleColor(ParticleType type, float alpha = 1.0f) {
    float r, g, b;
    ParticlePool::defaultColor(type, r, g, b);
    if (alpha < 0.999f) {
        const float fade = std::clamp(1.0f - alpha, 0.0f, 1.0f);
        const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const float sat_boost = 0.32f * fade;
        const float gain = 1.0f + 0.22f * fade;
        r = std::clamp((r + (r - luma) * sat_boost) * gain, 0.0f, 1.0f);
        g = std::clamp((g + (g - luma) * sat_boost) * gain, 0.0f, 1.0f);
        b = std::clamp((b + (b - luma) * sat_boost) * gain, 0.0f, 1.0f);
    }
    return {r, g, b, alpha};
}

static ImVec4 qcdColorSwatch(QcdColor color, float alpha = 1.0f) {
    float r = 1.0f, g = 1.0f, b = 1.0f;
    qcd::rgb(color, r, g, b);
    if (alpha < 0.999f) {
        const float fade = std::clamp(1.0f - alpha, 0.0f, 1.0f);
        const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        r = std::clamp((r + (r - luma) * 0.34f * fade) * (1.0f + 0.24f * fade), 0.0f, 1.0f);
        g = std::clamp((g + (g - luma) * 0.34f * fade) * (1.0f + 0.24f * fade), 0.0f, 1.0f);
        b = std::clamp((b + (b - luma) * 0.34f * fade) * (1.0f + 0.24f * fade), 0.0f, 1.0f);
    }
    return {r, g, b, alpha};
}

static ImVec4 particleQcdSwatch(ParticleType type, QcdColor color,
                                QcdColor anticolor = QcdColor::NONE,
                                float alpha = 1.0f) {
    float r = 1.0f, g = 1.0f, b = 1.0f;
    ParticlePool::applyQcdTint(type, color, anticolor, r, g, b);
    if (alpha < 0.999f) {
        const float fade = std::clamp(1.0f - alpha, 0.0f, 1.0f);
        const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        r = std::clamp((r + (r - luma) * 0.30f * fade) * (1.0f + 0.20f * fade), 0.0f, 1.0f);
        g = std::clamp((g + (g - luma) * 0.30f * fade) * (1.0f + 0.20f * fade), 0.0f, 1.0f);
        b = std::clamp((b + (b - luma) * 0.30f * fade) * (1.0f + 0.20f * fade), 0.0f, 1.0f);
    }
    return {r, g, b, alpha};
}

static const char* qcdColorLabel(QcdColor color) {
    switch (color) {
        case QcdColor::RED: return "Red";
        case QcdColor::GREEN: return "Green";
        case QcdColor::BLUE: return "Blue";
        case QcdColor::ANTI_RED: return "Anti-Red";
        case QcdColor::ANTI_GREEN: return "Anti-Green";
        case QcdColor::ANTI_BLUE: return "Anti-Blue";
        default: return "Neutral";
    }
}

static const char* directionalGluonLabel(QcdColor color, QcdColor anticolor) {
    if (color == QcdColor::RED && anticolor == QcdColor::ANTI_GREEN) return "R-Gbar";
    if (color == QcdColor::RED && anticolor == QcdColor::ANTI_BLUE) return "R-Bbar";
    if (color == QcdColor::GREEN && anticolor == QcdColor::ANTI_RED) return "G-Rbar";
    if (color == QcdColor::GREEN && anticolor == QcdColor::ANTI_BLUE) return "G-Bbar";
    if (color == QcdColor::BLUE && anticolor == QcdColor::ANTI_RED) return "B-Rbar";
    if (color == QcdColor::BLUE && anticolor == QcdColor::ANTI_GREEN) return "B-Gbar";
    return "Other Gluon";
}

// Formata um número em notação científica (ex: 1.23 x 10^8)
static void fmtSci(char* buf, size_t len, double v) {
    if (!std::isfinite(v)) { snprintf(buf, len, "nan"); return; }
    if (v == 0.0) { snprintf(buf, len, "0"); return; }
    int exp = static_cast<int>(std::floor(std::log10(std::abs(v))));
    double mantissa = v / std::pow(10.0, exp);
    if (std::abs(mantissa) >= 9.995) {
        mantissa /= 10.0;
        ++exp;
    }
    snprintf(buf, len, "%.3g x 10^%d", mantissa, exp);
}

constexpr float kSidePanelWidth = 300.0f;
constexpr float kLateTuningHeight = 272.0f;
constexpr float kCompactLabelX = 112.0f;

struct OverlayWindowLayout {
    const char* name;
    ImVec2 pos;
    ImVec2 size;
    float bg_alpha;
    ImGuiCond pos_condition;
    ImGuiCond size_condition;
};

static void primeWindowLayout(const OverlayWindowLayout& layout, bool force_defaults) {
    ImGui::SetNextWindowPos(layout.pos, force_defaults ? ImGuiCond_Always : layout.pos_condition);
    ImGui::SetNextWindowSize(layout.size, force_defaults ? ImGuiCond_Always : layout.size_condition);
    ImGui::SetNextWindowBgAlpha(layout.bg_alpha);
}

static const char* LATE_TUNING_PRESETS[] = {
    "Custom",
    "Planck-like",
    "Patchy EoR",
    "AGN-heavy"
};

static void applyLateRegimePreset(Universe::VisualTuning& visual, int preset_index) {
    visual = Universe::VisualTuning{};
    visual.preset_index = preset_index;

    switch (preset_index) {
        case 1: // Planck-like
            visual.exposure_multiplier = 0.92f;
            visual.volume_opacity_multiplier = 0.82f;
            visual.cmb_visibility_width = 0.72f;
            visual.cmb_flash_strength = 0.78f;
            visual.reionization_ionization_force = 0.88f;
            visual.reionization_front_anisotropy = 0.82f;
            visual.halo_visibility = 0.90f;
            visual.halo_axis_ratio = 1.10f;
            break;
        case 2: // Patchy EoR
            visual.exposure_multiplier = 1.06f;
            visual.volume_opacity_multiplier = 1.18f;
            visual.cmb_visibility_width = 1.10f;
            visual.cmb_flash_strength = 1.00f;
            visual.reionization_ionization_force = 1.34f;
            visual.reionization_front_anisotropy = 1.55f;
            visual.halo_visibility = 1.14f;
            visual.halo_axis_ratio = 1.34f;
            break;
        case 3: // AGN-heavy
            visual.exposure_multiplier = 1.10f;
            visual.volume_opacity_multiplier = 1.08f;
            visual.cmb_visibility_width = 0.94f;
            visual.cmb_flash_strength = 1.12f;
            visual.reionization_ionization_force = 1.70f;
            visual.reionization_front_anisotropy = 1.32f;
            visual.halo_visibility = 1.30f;
            visual.halo_axis_ratio = 1.48f;
            break;
        default:
            visual.preset_index = 0;
            break;
    }
}

static bool compactSliderFloat(const char* label, const char* id, float& value,
                               float min_value, float max_value, const char* format) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kCompactLabelX);
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat(id, &value, min_value, max_value, format);
}

static bool compactCombo(const char* label, const char* id, int& value,
                         const char* const items[], int items_count) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kCompactLabelX);
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::Combo(id, &value, items, items_count);
}

static bool compactCheckbox(const char* label, const char* id, bool& value) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kCompactLabelX);
    return ImGui::Checkbox(id, &value);
}

int RegimeOverlay::consumePendingJumpRegime() {
    int pending = pending_jump_regime_;
    pending_jump_regime_ = -1;
    return pending;
}

void RegimeOverlay::requestJumpToRegime(int regime_index) {
    pending_jump_regime_ = std::clamp(regime_index, 0, CosmicClock::LAST_REGIME_INDEX);
}

void RegimeOverlay::render(CosmicClock& clock, RegimeManager& mgr, Universe& universe, Camera& camera) {
    if (!visible) return;

    ImGuiIO& io = ImGui::GetIO();
    const OverlayWindowLayout hud_layout{
        "##CosmicHUD",
        {0.0f, 0.0f},
        {static_cast<float>(io.DisplaySize.x), 150.0f},
        0.72f,
        ImGuiCond_Always,
        ImGuiCond_Always,
    };
    const OverlayWindowLayout composition_layout{
        "##Cosmic Composition",
        {10.0f, 160.0f},
        {kSidePanelWidth, 210.0f},
        0.72f,
        ImGuiCond_FirstUseEver,
        ImGuiCond_FirstUseEver,
    };
    const OverlayWindowLayout late_tuning_layout{
        "##Late Regime Tuning",
        {10.0f, 360.0f},
        {kSidePanelWidth, kLateTuningHeight},
        0.72f,
        ImGuiCond_FirstUseEver,
        ImGuiCond_Always,
    };
    const OverlayWindowLayout perf_layout{
        "##Perf",
        {io.DisplaySize.x - 250.0f, 160.0f},
        {230.0f, 130.0f},
        0.6f,
        ImGuiCond_Always,
        ImGuiCond_Always,
    };

    primeWindowLayout(hud_layout, false);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove   | ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##CosmicHUD", nullptr, flags)) {
        drawTimeline(clock, mgr);
        ImGui::Separator();
        drawTimeControls(clock, mgr, universe, camera);
        ImGui::SameLine();
        drawPhysicsInfo(clock, mgr, universe);
    }
    ImGui::End();

    // Painel de composição (visível em todos os regimes)
    primeWindowLayout(composition_layout, false);
    if (ImGui::Begin("##Cosmic Composition", nullptr, 
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        drawCompositionTable(mgr, universe);
    }
    ImGui::End();

    if (universe.regime_index >= 5) {
        primeWindowLayout(late_tuning_layout, false);
        if (ImGui::Begin("##Late Regime Tuning", nullptr,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
            drawVisualTuning(universe);
        }
        ImGui::End();
    }

    // Overlay de desempenho (canto superior direito)
    primeWindowLayout(perf_layout, false);
    if (ImGui::Begin("##Perf", nullptr, flags)) {
        drawPerformanceStats(universe);
    }
    ImGui::End();
}

void RegimeOverlay::drawTimeline(CosmicClock& clock, RegimeManager& mgr) {
    float total_w = ImGui::GetContentRegionAvail().x;
    const bool in_transition = mgr.isInTransition();
    const int visual_regime = std::clamp(mgr.getVisualRegimeIndex(), 0, CosmicClock::LAST_REGIME_INDEX);
    const int incoming_regime = std::clamp(mgr.getIncomingRegimeIndex(), 0, CosmicClock::LAST_REGIME_INDEX);
    const float transition_t = std::clamp(mgr.getTransitionProgress(), 0.0f, 1.0f);

    // Desenhar segmentos de regime
    const ImVec4 regime_colors[CosmicClock::REGIME_COUNT] = {
        {0.88f, 0.95f, 0.56f, 1.0f},
        {1.00f, 0.62f, 0.22f, 1.0f},
        {0.30f, 0.90f, 0.96f, 1.0f},
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
        if (in_transition && i == visual_regime) {
            float weight = 1.0f + 0.4f * (1.0f - transition_t);
            c.x = std::min(1.0f, c.x * weight);
            c.y = std::min(1.0f, c.y * weight);
            c.z = std::min(1.0f, c.z * weight);
        } else if (in_transition && i == incoming_regime) {
            float weight = 0.9f + 0.5f * transition_t;
            c.x = std::min(1.0f, c.x * weight);
            c.y = std::min(1.0f, c.y * weight);
            c.z = std::min(1.0f, c.z * weight);
        } else if (i == visual_regime) {
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
    const int current_regime = visual_regime;
    const float regime_progress = in_transition
        ? 0.0f
        : static_cast<float>(std::clamp(clock.getRegimeProgress(), 0.0, 1.0));
    marker_x += static_cast<float>(current_regime) * (segment_w + segment_gap);
    marker_x += regime_progress * segment_w;
    if (in_transition) {
        marker_x = x0 + static_cast<float>(visual_regime + 1) * (segment_w + segment_gap) - segment_gap * 0.5f;
    }
    dl->AddLine({marker_x, y0}, {marker_x, y0+h+4}, IM_COL32(255,255,0,255), 2.0f);
    dl->AddTriangleFilled({marker_x, y0+h+4},
                          {marker_x-5, y0+h+12},
                          {marker_x+5, y0+h+12},
                          IM_COL32(255,255,0,200));
    ImGui::Dummy({total_w, h+14});
}

void RegimeOverlay::drawTimeControls(CosmicClock& clock, RegimeManager& mgr, Universe& universe, Camera& camera) {
    (void)universe;
    (void)camera;
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
        int rewind_regime = std::max(0, mgr.getVisualRegimeIndex() - 1);
        requestJumpToRegime(rewind_regime);
    }

    // Predefinições de velocidade
    ImGui::SameLine();
    if (ImGui::Button("SPEED")) {
        speed_preset_index_ = (speed_preset_index_ + 1) % 5;
        clock.applySpeedPreset(static_cast<CosmicClock::SpeedPreset>(speed_preset_index_));
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(speedPresetLabel(speed_preset_index_));

    // Controle deslizante do multiplicador relativo ao tempo-base do regime atual.
    int current_regime = std::clamp(mgr.getCurrentRegimeIndex(), 0, CosmicClock::LAST_REGIME_INDEX);
    double base_scale = CosmicClock::defaultScaleForRegimeIndex(current_regime);
    double base_window_seconds = CosmicClock::defaultRealSecondsForRegime(current_regime);
    double remaining_window_seconds = clock.getEstimatedRealSecondsToNextRegime();
    float log_multiplier = std::log10(static_cast<float>(std::max(clock.getSpeedMultiplier(), 1e-6)));
    ImGui::PushItemWidth(200.0f);
    if (ImGui::SliderFloat("Speed multiplier (log)", &log_multiplier, -6.0f, 6.0f, "x%.3g")) {
        clock.setTimeScale(base_scale * std::pow(10.0, static_cast<double>(log_multiplier)));
        speed_preset_index_ = -1;
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::Text("base %.2e | current %.2e", base_scale, clock.getTimeScale());
    ImGui::Text("Window %.0fs @ x1 | remaining %.1fs", base_window_seconds, remaining_window_seconds);

    // Botões de salto
    ImGui::Text("Jump to:");
    for (int i = 0; i < CosmicClock::REGIME_COUNT; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::SmallButton(REGIME_NAMES[i])) {
            requestJumpToRegime(i);
        }
    }

    ImGui::EndGroup();
}

void RegimeOverlay::drawPhysicsInfo(const CosmicClock& clock, const RegimeManager& mgr, const Universe& /*universe*/) {
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
    ImGui::Text("H = %s s^(-1)", Hbuf);
    if (mgr.isInTransition()) {
        ImGui::Text("Regime: %s -> %s (%.0f%%)",
                    REGIME_NAMES[mgr.getVisualRegimeIndex()],
                    REGIME_NAMES[mgr.getIncomingRegimeIndex()],
                    mgr.getTransitionProgress() * 100.0f);
    } else {
        ImGui::Text("Regime: %s", REGIME_NAMES[mgr.getCurrentRegimeIndex()]);
    }
    ImGui::EndGroup();
}

void RegimeOverlay::drawCompositionTable(const RegimeManager& mgr, const Universe& universe) {
    int regime = mgr.getIncomingRegimeIndex();
    std::unordered_map<ParticleType, int> counts;
    int total = 0;
    const ParticlePool& pp = universe.particles;
    for (size_t i = 0; i < pp.x.size(); ++i) {
        if (!(pp.flags[i] & PF_ACTIVE)) continue;
        counts[pp.type[i]]++;
        ++total;
    }
    
    // Auxiliar progress bar
    auto bar = [&](const char* label, double val, ImVec4 color) {
        ImGui::TextColored(color, "%-10s", label);
        ImGui::SameLine(110);
        char buf[32]; snprintf(buf, sizeof(buf), "%.2f%%", val * 100.0);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::ProgressBar(static_cast<float>(val), {155, 14}, buf);
        ImGui::PopStyleColor();
    };

    auto sumTypes = [&](std::initializer_list<ParticleType> types) {
        int sum = 0;
        for (ParticleType type : types) sum += counts[type];
        return sum;
    };

    auto renderGroup = [&](const char* label, int count, ImVec4 color) {
        if (count <= 0) return;
        bar(label, static_cast<double>(count) / static_cast<double>(std::max(total, 1)), color);
    };

    if (regime == 0) {
        ImGui::Text("Inflation Era");
        ImGui::Text("Scalar Field (Inflaton) Vacuum Energy");
        bar("Inflaton", 1.0, {0.2f, 0.4f, 1.0f, 1.0f});
        return;
    }

    if (regime == 4) {
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

    if (total == 0) {
        ImGui::Text("Awaiting Particle Generation...");
        return;
    }

    ImGui::Text("Particle Distribution (%d total)", total);
    
    // Função local para mapear tipo -> cor média renderizada & nome
    auto renderType = [&](ParticleType type, const char* name) {
        if (counts[type] > 0) {
            double frac = static_cast<double>(counts[type]) / total;
            bar(name, frac, particleColor(type));
        }
    };

    if (regime == 1) {
        renderGroup("Quarks", sumTypes({
            ParticleType::QUARK_U, ParticleType::QUARK_D, ParticleType::QUARK_S,
            ParticleType::QUARK_C, ParticleType::QUARK_B, ParticleType::QUARK_T}),
            particleColor(ParticleType::QUARK_U));
        renderGroup("Anti-Quarks", sumTypes({
            ParticleType::ANTIQUARK_U, ParticleType::ANTIQUARK_D, ParticleType::ANTIQUARK_S,
            ParticleType::ANTIQUARK_C, ParticleType::ANTIQUARK_B, ParticleType::ANTIQUARK_T,
            ParticleType::ANTIQUARK}),
            particleColor(ParticleType::ANTIQUARK));
        renderGroup("Gluons", counts[ParticleType::GLUON], particleColor(ParticleType::GLUON));
        renderGroup("Bosons", sumTypes({
            ParticleType::PHOTON, ParticleType::W_BOSON_POS, ParticleType::W_BOSON_NEG,
            ParticleType::Z_BOSON, ParticleType::HIGGS_BOSON}),
            particleColor(ParticleType::PHOTON));
        renderGroup("Leptons", sumTypes({
            ParticleType::ELECTRON, ParticleType::POSITRON, ParticleType::MUON,
            ParticleType::ANTIMUON, ParticleType::TAU, ParticleType::ANTITAU}),
            particleColor(ParticleType::ELECTRON));
        renderGroup("Neutrinos", sumTypes({
            ParticleType::NEUTRINO, ParticleType::NEUTRINO_E, ParticleType::ANTINEUTRINO_E,
            ParticleType::NEUTRINO_MU, ParticleType::ANTINEUTRINO_MU,
            ParticleType::NEUTRINO_TAU, ParticleType::ANTINEUTRINO_TAU}),
            particleColor(ParticleType::NEUTRINO_E));
    } else if (regime == 2) {
        renderGroup("Quarks", sumTypes({
            ParticleType::QUARK_U, ParticleType::QUARK_D, ParticleType::QUARK_S, ParticleType::QUARK_C,
            ParticleType::ANTIQUARK_U, ParticleType::ANTIQUARK_D, ParticleType::ANTIQUARK_S, ParticleType::ANTIQUARK_C,
            ParticleType::ANTIQUARK}), particleColor(ParticleType::QUARK_S));
        renderGroup("Gluons", counts[ParticleType::GLUON], particleColor(ParticleType::GLUON));
        renderGroup("Charged Lept.", sumTypes({
            ParticleType::ELECTRON, ParticleType::POSITRON, ParticleType::MUON,
            ParticleType::ANTIMUON, ParticleType::TAU, ParticleType::ANTITAU}),
            particleColor(ParticleType::ELECTRON));
        renderGroup("Neutrinos", sumTypes({
            ParticleType::NEUTRINO_E, ParticleType::ANTINEUTRINO_E,
            ParticleType::NEUTRINO_MU, ParticleType::ANTINEUTRINO_MU,
            ParticleType::NEUTRINO_TAU, ParticleType::ANTINEUTRINO_TAU}),
            particleColor(ParticleType::NEUTRINO_E));
        renderGroup("Bosons", sumTypes({
            ParticleType::PHOTON, ParticleType::W_BOSON_POS,
            ParticleType::W_BOSON_NEG, ParticleType::Z_BOSON}),
            particleColor(ParticleType::PHOTON));
    } else if (regime == 3) {
        renderType(ParticleType::QUARK_U, "Quark Up");
        renderType(ParticleType::QUARK_D, "Quark Down");
        renderType(ParticleType::QUARK_S, "Quark Strange");
        renderType(ParticleType::ANTIQUARK_U, "Anti-Up");
        renderType(ParticleType::ANTIQUARK_D, "Anti-Down");
        renderType(ParticleType::ANTIQUARK_S, "Anti-Strange");
        renderType(ParticleType::GLUON,   "Gluon");
        renderType(ParticleType::PHOTON,  "Photons");
        renderType(ParticleType::ELECTRON,"Electrons");
        renderType(ParticleType::POSITRON,"Positrons");

        std::unordered_map<QcdColor, int> quark_colors;
        struct GluonKey {
            QcdColor color;
            QcdColor anticolor;
            bool operator==(const GluonKey& other) const {
                return color == other.color && anticolor == other.anticolor;
            }
        };
        struct GluonKeyHash {
            size_t operator()(const GluonKey& key) const {
                return (static_cast<size_t>(key.color) << 8) ^ static_cast<size_t>(key.anticolor);
            }
        };
        std::unordered_map<GluonKey, int, GluonKeyHash> gluon_colors;
        int colored_quarks = 0;
        int actual_gluons = 0;

        for (size_t i = 0; i < pp.x.size(); ++i) {
            if (!(pp.flags[i] & PF_ACTIVE)) continue;
            if (pp.type[i] == ParticleType::QUARK_U || pp.type[i] == ParticleType::QUARK_D ||
                pp.type[i] == ParticleType::QUARK_S || pp.type[i] == ParticleType::ANTIQUARK_U ||
                pp.type[i] == ParticleType::ANTIQUARK_D || pp.type[i] == ParticleType::ANTIQUARK_S ||
                pp.type[i] == ParticleType::ANTIQUARK) {
                if (pp.qcd_color[i] != QcdColor::NONE) {
                    quark_colors[pp.qcd_color[i]]++;
                    ++colored_quarks;
                } else if (pp.qcd_anticolor[i] != QcdColor::NONE) {
                    quark_colors[qcd::receiverColorFromAnticolor(pp.qcd_anticolor[i])]++;
                    ++colored_quarks;
                }
            } else if (pp.type[i] == ParticleType::GLUON) {
                if (pp.qcd_color[i] == QcdColor::NONE || pp.qcd_anticolor[i] == QcdColor::NONE) continue;
                GluonKey key{pp.qcd_color[i], pp.qcd_anticolor[i]};
                gluon_colors[key]++;
                ++actual_gluons;
            }
        }

        if (colored_quarks > 0) {
            ImGui::Spacing();
            ImGui::TextUnformatted("QCD Color Distribution");
            auto renderColorBar = [&](QcdColor color) {
                auto it = quark_colors.find(color);
                if (it == quark_colors.end() || it->second <= 0) return;
                bar(qcdColorLabel(color),
                    static_cast<double>(it->second) / static_cast<double>(colored_quarks),
                    qcdColorSwatch(color));
            };
            renderColorBar(QcdColor::RED);
            renderColorBar(QcdColor::GREEN);
            renderColorBar(QcdColor::BLUE);
        }

        if (actual_gluons > 0) {
            ImGui::Spacing();
            ImGui::TextUnformatted("Directional Gluons");
            const GluonKey ordered_gluons[] = {
                {QcdColor::RED, QcdColor::ANTI_GREEN},
                {QcdColor::RED, QcdColor::ANTI_BLUE},
                {QcdColor::GREEN, QcdColor::ANTI_RED},
                {QcdColor::GREEN, QcdColor::ANTI_BLUE},
                {QcdColor::BLUE, QcdColor::ANTI_RED},
                {QcdColor::BLUE, QcdColor::ANTI_GREEN},
            };
            for (const GluonKey& key : ordered_gluons) {
                auto it = gluon_colors.find(key);
                if (it == gluon_colors.end() || it->second <= 0) continue;
                bar(directionalGluonLabel(key.color, key.anticolor),
                    static_cast<double>(it->second) / static_cast<double>(actual_gluons),
                    particleQcdSwatch(ParticleType::GLUON, key.color, key.anticolor));
            }
        }
    } else if (regime == 5) {
        renderType(ParticleType::DEUTERIUM,     "Deuterium");
        renderType(ParticleType::HELIUM3,       "Helium-3");
        renderType(ParticleType::HELIUM4NUCLEI, "He-4 Nuclei");
        renderType(ParticleType::LITHIUM7,      "Lithium-7");
        renderType(ParticleType::PHOTON,        "Photons");
        renderType(ParticleType::PROTON,        "Protons");
        renderType(ParticleType::ELECTRON,      "Electrons");
        renderType(ParticleType::NEUTRINO,      "Neutrinos");
    } else if (regime == 6) {
        renderType(ParticleType::DARK_MATTER,   "Dark Matter");
        renderType(ParticleType::GAS,           "Neutral Gas");
    } else if (regime == 7) {
        renderType(ParticleType::DARK_MATTER,   "Dark Matter");
        renderType(ParticleType::GAS,           "Ionized Gas");
        renderType(ParticleType::STAR,          "First Stars");
        renderType(ParticleType::BLACKHOLE,     "Seed BH");
    } else if (regime == 8) {
        renderType(ParticleType::DARK_MATTER,   "Dark Matter");
        renderType(ParticleType::GAS,           "Gas");
        renderType(ParticleType::STAR,          "Stars");
        renderType(ParticleType::BLACKHOLE,     "Black Holes");
    }
}

void RegimeOverlay::drawPerformanceStats(const Universe& universe) {
    auto coloredStat = [&](ImVec4 color, const char* fmt, auto... args) {
        ImGui::TextColored(color, fmt, args...);
    };

    ImGui::Text("Quality: %s", universe.quality.name);
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

    auto sumTypes = [&](std::initializer_list<ParticleType> types) {
        int sum = 0;
        for (ParticleType type : types) sum += counts[type];
        return sum;
    };

    // Mostrar informações específicas por regime, usando contagens reais
    if (universe.regime_index == 0) {
        ImGui::Text("Inflation field: %dx%d", universe.phi_NX, universe.phi_NY);
        ImGui::Text("Phi samples: %zu", universe.phi_field.size());
        ImGui::Text("Active particles: %d", total_active);
    } else if (universe.regime_index == 1) {
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::QUARK_U), "Quarks: %d", sumTypes({
            ParticleType::QUARK_U, ParticleType::QUARK_D, ParticleType::QUARK_S,
            ParticleType::QUARK_C, ParticleType::QUARK_B, ParticleType::QUARK_T}));
        coloredStat(particleColor(ParticleType::ANTIQUARK), "Anti-Quarks: %d", sumTypes({
            ParticleType::ANTIQUARK_U, ParticleType::ANTIQUARK_D, ParticleType::ANTIQUARK_S,
            ParticleType::ANTIQUARK_C, ParticleType::ANTIQUARK_B, ParticleType::ANTIQUARK_T,
            ParticleType::ANTIQUARK}));
        coloredStat(particleColor(ParticleType::GLUON), "Gluons: %d", counts[ParticleType::GLUON]);
        coloredStat(particleColor(ParticleType::PHOTON), "Bosons: %d", sumTypes({
            ParticleType::PHOTON, ParticleType::W_BOSON_POS, ParticleType::W_BOSON_NEG,
            ParticleType::Z_BOSON, ParticleType::HIGGS_BOSON}));
        coloredStat(particleColor(ParticleType::ELECTRON), "Leptons: %d", sumTypes({
            ParticleType::ELECTRON, ParticleType::POSITRON, ParticleType::MUON,
            ParticleType::ANTIMUON, ParticleType::TAU, ParticleType::ANTITAU}));
    } else if (universe.regime_index == 2) {
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::ELECTRON), "Charged leptons: %d", sumTypes({
            ParticleType::ELECTRON, ParticleType::POSITRON, ParticleType::MUON,
            ParticleType::ANTIMUON, ParticleType::TAU, ParticleType::ANTITAU}));
        coloredStat(particleColor(ParticleType::NEUTRINO_E), "Neutrinos: %d", sumTypes({
            ParticleType::NEUTRINO_E, ParticleType::ANTINEUTRINO_E,
            ParticleType::NEUTRINO_MU, ParticleType::ANTINEUTRINO_MU,
            ParticleType::NEUTRINO_TAU, ParticleType::ANTINEUTRINO_TAU}));
        coloredStat(particleColor(ParticleType::GLUON), "Gluons: %d", counts[ParticleType::GLUON]);
        coloredStat(particleColor(ParticleType::PHOTON), "Photons: %d", counts[ParticleType::PHOTON]);
    } else if (universe.regime_index == 3) {
        int qu_u = counts[ParticleType::QUARK_U];
        int qu_d = counts[ParticleType::QUARK_D];
        int qu_s = counts[ParticleType::QUARK_S];
        int anti = counts[ParticleType::ANTIQUARK_U] + counts[ParticleType::ANTIQUARK_D] + counts[ParticleType::ANTIQUARK_S] + counts[ParticleType::ANTIQUARK];
        int gluons = counts[ParticleType::GLUON];
        int red = 0, green = 0, blue = 0;
        for (size_t i = 0; i < pp.x.size(); ++i) {
            if (!(pp.flags[i] & PF_ACTIVE)) continue;
            if (pp.type[i] != ParticleType::QUARK_U && pp.type[i] != ParticleType::QUARK_D &&
                pp.type[i] != ParticleType::QUARK_S && pp.type[i] != ParticleType::ANTIQUARK_U &&
                pp.type[i] != ParticleType::ANTIQUARK_D && pp.type[i] != ParticleType::ANTIQUARK_S &&
                pp.type[i] != ParticleType::ANTIQUARK) continue;
            QcdColor display_color = (pp.qcd_color[i] != QcdColor::NONE) ? pp.qcd_color[i]
                                                                         : qcd::receiverColorFromAnticolor(pp.qcd_anticolor[i]);
            switch (display_color) {
                case QcdColor::RED: ++red; break;
                case QcdColor::GREEN: ++green; break;
                case QcdColor::BLUE: ++blue; break;
                default: break;
            }
        }
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::QUARK_U), "Quark Up: %d", qu_u);
        coloredStat(particleColor(ParticleType::QUARK_D), "Quark Down: %d", qu_d);
        coloredStat(particleColor(ParticleType::QUARK_S), "Quark Strange: %d", qu_s);
        coloredStat(particleColor(ParticleType::ANTIQUARK), "Anti-Quarks: %d", anti);
        coloredStat(particleColor(ParticleType::GLUON), "Gluons: %d", gluons);
        ImGui::TextColored(qcdColorSwatch(QcdColor::RED), "Red: %d", red);
        ImGui::SameLine();
        ImGui::TextColored(qcdColorSwatch(QcdColor::GREEN), "Green: %d", green);
        ImGui::SameLine();
        ImGui::TextColored(qcdColorSwatch(QcdColor::BLUE), "Blue: %d", blue);
    } else if (universe.regime_index == 4) {
        int protons = counts[ParticleType::PROTON];
        int neutrons = counts[ParticleType::NEUTRON];
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::PROTON), "Protons: %d", protons);
        coloredStat(particleColor(ParticleType::NEUTRON), "Neutrons: %d", neutrons);
        ImGui::Text("Abundances Xp: %.3f Xn: %.3f", universe.abundances.Xp, universe.abundances.Xn);
    } else if (universe.regime_index == 5) {
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::PHOTON), "Photons: %d", counts[ParticleType::PHOTON]);
        coloredStat(particleColor(ParticleType::PROTON), "Protons: %d", counts[ParticleType::PROTON]);
        coloredStat(particleColor(ParticleType::ELECTRON), "Electrons: %d", counts[ParticleType::ELECTRON]);
    } else if (universe.regime_index == 6) {
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::DARK_MATTER), "Dark Matter: %d", counts[ParticleType::DARK_MATTER]);
        coloredStat(particleColor(ParticleType::GAS), "Neutral Gas: %d", counts[ParticleType::GAS]);
        ImGui::TextDisabled("CMB desacoplado; a cena mostra gas neutro e materia escura.");
    } else if (universe.regime_index == 7) {
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::STAR), "First Stars: %d", counts[ParticleType::STAR]);
        coloredStat(particleColor(ParticleType::GAS), "Ionized Gas: %d", counts[ParticleType::GAS]);
    } else if (universe.regime_index == 8) {
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::DARK_MATTER), "Dark Matter: %d", counts[ParticleType::DARK_MATTER]);
        coloredStat(particleColor(ParticleType::GAS), "Gas: %d", counts[ParticleType::GAS]);
        coloredStat(particleColor(ParticleType::STAR), "Stars: %d", counts[ParticleType::STAR]);
        coloredStat(particleColor(ParticleType::BLACKHOLE), "Black Holes: %d", counts[ParticleType::BLACKHOLE]);
    } else {
        ImGui::Text("Active particles: %d", total_active);
    }
}

void RegimeOverlay::drawVisualTuning(Universe& universe) {
    auto& visual = universe.visual;
    bool custom_edit = false;

    if (ImGui::Button("Reset visual")) {
        visual = Universe::VisualTuning{};
    }
    ImGui::SameLine();
    if (ImGui::Button("Planck")) {
        applyLateRegimePreset(visual, 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Patchy")) {
        applyLateRegimePreset(visual, 2);
    }
    ImGui::SameLine();
    if (ImGui::Button("AGN")) {
        applyLateRegimePreset(visual, 3);
    }

    if (compactCombo("Preset", "##LatePreset", visual.preset_index,
                     LATE_TUNING_PRESETS,
                     static_cast<int>(std::size(LATE_TUNING_PRESETS)))) {
        applyLateRegimePreset(visual, visual.preset_index);
    }

    ImGui::SeparatorText("Post-process");
    custom_edit |= compactSliderFloat("Exposure", "##Exposure", visual.exposure_multiplier, 0.35f, 2.50f, "%.2fx");
    custom_edit |= compactSliderFloat("Vol. opacity", "##VolumeOpacity", visual.volume_opacity_multiplier, 0.20f, 2.50f, "%.2fx");
    custom_edit |= compactSliderFloat("CMB width", "##CmbWidth", visual.cmb_visibility_width, 0.35f, 2.50f, "%.2fx");
    custom_edit |= compactSliderFloat("Flash gain", "##CmbFlashStrength", visual.cmb_flash_strength, 0.20f, 2.50f, "%.2fx");

    ImGui::SeparatorText("Reionization");
    custom_edit |= compactSliderFloat("Ionization", "##IonizationForce", visual.reionization_ionization_force, 0.15f, 3.00f, "%.2fx");
    custom_edit |= compactSliderFloat("Anisotropy", "##FrontAnisotropy", visual.reionization_front_anisotropy, 0.00f, 2.50f, "%.2fx");

    ImGui::SeparatorText("Halos");
    custom_edit |= compactCheckbox("Show halos", "##ShowHalos", visual.show_halos);
    custom_edit |= compactSliderFloat("Halo gain", "##HaloVisibility", visual.halo_visibility, 0.0f, 2.5f, "%.2fx");
    custom_edit |= compactSliderFloat("Axis ratio", "##HaloAxisRatio", visual.halo_axis_ratio, 0.65f, 2.40f, "%.2fx");

    if (custom_edit) {
        visual.preset_index = 0;
    }
}
