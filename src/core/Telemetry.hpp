#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct Universe;
struct RendererDiagnostics;

struct TelemetryFrameTimings {
    double input_ms = 0.0;
    double deferred_jump_ms = 0.0;
    double sim_total_ms = 0.0;
    double clock_step_ms = 0.0;
    double manager_tick_ms = 0.0;
    double regime_update_ms = 0.0;
    double auto_frame_ms = 0.0;
    double render_setup_ms = 0.0;
    double render_ms = 0.0;
    double imgui_ms = 0.0;
    double video_capture_ms = 0.0;
    double swap_buffers_ms = 0.0;
    double frame_ms = 0.0;
};

class TelemetrySession {
public:
    bool start(const std::filesystem::path& project_root,
               const std::filesystem::path& requested_output_path,
               const char* build_quality,
               std::uint32_t seed,
               int window_width,
               int window_height,
               bool video_enabled,
               bool panoramic_camera,
               const char* gl_version,
               const char* gl_renderer,
               const char* gl_vendor,
               const RendererDiagnostics& renderer_diagnostics);

    bool enabled() const { return enabled_; }
    const std::filesystem::path& outputPath() const { return output_path_; }

    void noteCheckpoint(const std::string& label,
                        const Universe& universe,
                        const RendererDiagnostics& renderer_diagnostics);
    void noteOperatorEvent(const std::string& event);
    void noteRegimeChange(int previous_regime,
                          int current_regime,
                          const Universe& universe);
    void noteShaderReload(const RendererDiagnostics& renderer_diagnostics);
    void recordFrame(std::uint64_t frame_index,
                     int sim_steps,
                     float real_dt,
                     const Universe& universe,
                     const RendererDiagnostics& renderer_diagnostics,
                     const TelemetryFrameTimings& timings);
    void finish(int exit_code,
                const Universe& universe,
                const RendererDiagnostics& renderer_diagnostics);

private:
    bool enabled_ = false;
    std::filesystem::path output_path_;
};
