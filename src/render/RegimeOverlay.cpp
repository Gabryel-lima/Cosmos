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

static const char* speedPresetLabel(int preset_index) {
    if (preset_index < 0 || preset_index >= static_cast<int>(std::size(SPEED_LABELS))) {
        return "CUSTOM";
    }
    return SPEED_LABELS[preset_index];
}

static ImVec4 particleColor(ParticleType type, float alpha = 1.0f) {
    float r, g, b;
    ParticlePool::defaultColor(type, r, g, b);
    return {r, g, b, alpha};
}

static ImVec4 qcdColorSwatch(QcdColor color, float alpha = 1.0f) {
    float r = 1.0f, g = 1.0f, b = 1.0f;
    qcd::rgb(color, r, g, b);
    return {r, g, b, alpha};
}

static ImVec4 particleQcdSwatch(ParticleType type, QcdColor color,
                                QcdColor anticolor = QcdColor::NONE,
                                float alpha = 1.0f) {
    float r = 1.0f, g = 1.0f, b = 1.0f;
    ParticlePool::applyQcdTint(type, color, anticolor, r, g, b);
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
        drawPhysicsInfo(clock, mgr, universe);
    }
    ImGui::End();

    // Painel de composição (visível em todos os regimes)
    ImGui::SetNextWindowPos({10, 210}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({300, 220}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.72f);
    if (ImGui::Begin("Cosmic Composition", nullptr, ImGuiWindowFlags_NoCollapse)) {
        drawCompositionTable(mgr, universe);
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
    const bool in_transition = mgr.isInTransition();
    const int visual_regime = std::clamp(mgr.getVisualRegimeIndex(), 0, CosmicClock::LAST_REGIME_INDEX);
    const int incoming_regime = std::clamp(mgr.getIncomingRegimeIndex(), 0, CosmicClock::LAST_REGIME_INDEX);
    const float transition_t = std::clamp(mgr.getTransitionProgress(), 0.0f, 1.0f);

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
        mgr.jumpToRegime(rewind_regime, clock, universe);
        SceneFrame scene_frame = Camera::estimateSceneFrame(universe);
        camera.applyState(camera.getSceneFittedState(rewind_regime, scene_frame));
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
    double base_scale = CosmicClock::DEFAULT_SCALE[static_cast<size_t>(current_regime)];
    float log_multiplier = std::log10(static_cast<float>(std::max(clock.getSpeedMultiplier(), 1e-6)));
    ImGui::PushItemWidth(200.0f);
    if (ImGui::SliderFloat("Speed multiplier (log)", &log_multiplier, -6.0f, 6.0f, "x%.3g")) {
        clock.setTimeScale(base_scale * std::pow(10.0, static_cast<double>(log_multiplier)));
        speed_preset_index_ = -1;
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::Text("base %.2e | current %.2e", base_scale, clock.getTimeScale());

    // Botões de salto
    ImGui::Text("Jump to:");
    for (int i = 0; i < CosmicClock::REGIME_COUNT; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::SmallButton(REGIME_NAMES[i])) {
            mgr.jumpToRegime(i, clock, universe);
            SceneFrame scene_frame = Camera::estimateSceneFrame(universe);
            camera.applyState(camera.getSceneFittedState(i, scene_frame));
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
        renderType(ParticleType::QUARK_U, "Quark Up");
        renderType(ParticleType::QUARK_D, "Quark Down");
        renderType(ParticleType::QUARK_S, "Quark Strange");
        renderType(ParticleType::GLUON,   "Gluon");
        renderType(ParticleType::PROTON,  "Protons");
        renderType(ParticleType::NEUTRON, "Neutrons");

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
                pp.type[i] == ParticleType::QUARK_S || pp.type[i] == ParticleType::ANTIQUARK) {
                if (pp.qcd_color[i] != QcdColor::NONE) {
                    quark_colors[pp.qcd_color[i]]++;
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
    } else if (regime == 3) {
        renderType(ParticleType::DEUTERIUM,     "Deuterium");
        renderType(ParticleType::HELIUM3,       "Helium-3");
        renderType(ParticleType::HELIUM4NUCLEI, "He-4 Nuclei");
        renderType(ParticleType::LITHIUM7,      "Lithium-7");
        renderType(ParticleType::PHOTON,        "Photons");
        renderType(ParticleType::PROTON,        "Protons");
        renderType(ParticleType::ELECTRON,      "Electrons");
        renderType(ParticleType::NEUTRINO,      "Neutrinos");
    } else if (regime == 4) {
        renderType(ParticleType::DARK_MATTER,   "Dark Matter");
        renderType(ParticleType::GAS,           "Neutral Gas");
        renderType(ParticleType::PHOTON,        "CMB Photons");
    } else if (regime == 5) {
        renderType(ParticleType::DARK_MATTER,   "Dark Matter");
        renderType(ParticleType::GAS,           "Ionized Gas");
        renderType(ParticleType::STAR,          "First Stars");
        renderType(ParticleType::BLACKHOLE,     "Seed BH");
    } else if (regime == 6) {
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
        int gluons = counts[ParticleType::GLUON];
        int red = 0, green = 0, blue = 0;
        for (size_t i = 0; i < pp.x.size(); ++i) {
            if (!(pp.flags[i] & PF_ACTIVE)) continue;
            if (pp.type[i] != ParticleType::QUARK_U && pp.type[i] != ParticleType::QUARK_D &&
                pp.type[i] != ParticleType::QUARK_S && pp.type[i] != ParticleType::ANTIQUARK) continue;
            switch (pp.qcd_color[i]) {
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
        coloredStat(particleColor(ParticleType::GLUON), "Gluons: %d", gluons);
        ImGui::TextColored(qcdColorSwatch(QcdColor::RED), "Red: %d", red);
        ImGui::SameLine();
        ImGui::TextColored(qcdColorSwatch(QcdColor::GREEN), "Green: %d", green);
        ImGui::SameLine();
        ImGui::TextColored(qcdColorSwatch(QcdColor::BLUE), "Blue: %d", blue);
    } else if (universe.regime_index == 2) {
        int protons = counts[ParticleType::PROTON];
        int neutrons = counts[ParticleType::NEUTRON];
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::PROTON), "Protons: %d", protons);
        coloredStat(particleColor(ParticleType::NEUTRON), "Neutrons: %d", neutrons);
        ImGui::Text("Abundances Xp: %.3f Xn: %.3f", universe.abundances.Xp, universe.abundances.Xn);
    } else if (universe.regime_index == 3) {
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::PHOTON), "Photons: %d", counts[ParticleType::PHOTON]);
        coloredStat(particleColor(ParticleType::PROTON), "Protons: %d", counts[ParticleType::PROTON]);
        coloredStat(particleColor(ParticleType::ELECTRON), "Electrons: %d", counts[ParticleType::ELECTRON]);
    } else if (universe.regime_index == 4) {
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::DARK_MATTER), "Dark Matter: %d", counts[ParticleType::DARK_MATTER]);
        coloredStat(particleColor(ParticleType::GAS), "Neutral Gas: %d", counts[ParticleType::GAS]);
    } else if (universe.regime_index == 5) {
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::STAR), "First Stars: %d", counts[ParticleType::STAR]);
        coloredStat(particleColor(ParticleType::GAS), "Ionized Gas: %d", counts[ParticleType::GAS]);
    } else if (universe.regime_index == 6) {
        ImGui::Text("Active particles: %d", total_active);
        coloredStat(particleColor(ParticleType::DARK_MATTER), "Dark Matter: %d", counts[ParticleType::DARK_MATTER]);
        coloredStat(particleColor(ParticleType::GAS), "Gas: %d", counts[ParticleType::GAS]);
        coloredStat(particleColor(ParticleType::STAR), "Stars: %d", counts[ParticleType::STAR]);
        coloredStat(particleColor(ParticleType::BLACKHOLE), "Black Holes: %d", counts[ParticleType::BLACKHOLE]);
    } else {
        ImGui::Text("Active particles: %d", total_active);
    }
}
