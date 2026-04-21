#pragma once
// src/render/RegimeOverlay.hpp — HUD Dear ImGui: linha do tempo, controles de tempo, estatísticas.

struct Universe;
class  CosmicClock;
class  RegimeManager;
class  Camera;

class RegimeOverlay {
public:
    /// Renderiza todos os painéis ImGui. Chamar entre ImGui::NewFrame() e ImGui::Render().
    void render(CosmicClock& clock, RegimeManager& mgr, Universe& universe, Camera& camera);

    bool visible = true;  // alternar com a tecla H

private:
    void drawTimeline(CosmicClock& clock, RegimeManager& mgr);
    void drawTimeControls(CosmicClock& clock, RegimeManager& mgr, Universe& universe, Camera& camera);
    void drawPhysicsInfo(const CosmicClock& clock, const Universe& universe);
    void drawAbundancePieChart(const Universe& universe);
    void drawPerformanceStats(const Universe& universe);

    // Estado do ciclo de predefinições de velocidade
    int speed_preset_index_ = 1;  // NORMAL
};
