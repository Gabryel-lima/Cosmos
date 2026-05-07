#include "Telemetry.hpp"

#include "RegimeConfig.hpp"
#include "Universe.hpp"
#include "../render/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace {

constexpr std::uint64_t kFrameSampleStride = 30;
constexpr std::uint64_t kTerminalSampleStride = 120;

enum SectionIndex : std::size_t {
    InputSection = 0,
    DeferredJumpSection,
    SimTotalSection,
    ClockStepSection,
    ManagerTickSection,
    RegimeUpdateSection,
    AutoFrameSection,
    RenderSetupSection,
    RenderSection,
    ImGuiSection,
    VideoCaptureSection,
    SwapBuffersSection,
    FrameTotalSection,
    SectionCount
};

struct SectionSummary {
    const char* label = "";
    double total_ms = 0.0;
    double min_ms = std::numeric_limits<double>::max();
    double max_ms = 0.0;
    std::uint64_t samples = 0;

    void addSample(double value_ms) {
        total_ms += value_ms;
        min_ms = std::min(min_ms, value_ms);
        max_ms = std::max(max_ms, value_ms);
        ++samples;
    }

    double averageMs() const {
        return samples > 0 ? (total_ms / static_cast<double>(samples)) : 0.0;
    }
};

struct GridDiagnostics {
    float min_value = 0.0f;
    float max_value = 0.0f;
    double mean_value = 0.0;
    std::size_t nonfinite_count = 0;
};

struct UniverseDiagnostics {
    std::size_t active_particles = 0;
    std::size_t flagged_active_particles = 0;
    std::size_t gas_particles = 0;
    std::size_t dark_matter_particles = 0;
    std::size_t star_particles = 0;
    std::size_t black_holes = 0;
    std::size_t photon_particles = 0;
    std::size_t baryon_particles = 0;
    std::size_t ionized_particles = 0;
    std::size_t collapsing_particles = 0;
    std::size_t protostars = 0;
    std::size_t compact_remnants = 0;
    std::size_t nonfinite_positions = 0;
    std::size_t nonfinite_velocities = 0;
    std::size_t negative_mass = 0;
    std::size_t out_of_bounds = 0;
    double total_mass = 0.0;
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;
    double min_z = 0.0;
    double max_z = 0.0;
    double max_speed = 0.0;
    GridDiagnostics density;
    GridDiagnostics ionization;
    GridDiagnostics emissivity;

    bool hasAnomaly() const {
        return nonfinite_positions > 0 ||
               nonfinite_velocities > 0 ||
               negative_mass > 0 ||
               out_of_bounds > 0;
    }
};

struct TelemetryState {
    bool enabled = false;
    std::filesystem::path output_path;
    std::ofstream output;
    std::array<SectionSummary, SectionCount> sections{{
        {"input"},
        {"deferred_jump"},
        {"sim_total"},
        {"clock_step"},
        {"manager_tick"},
        {"regime_update"},
        {"auto_frame"},
        {"render_setup"},
        {"render"},
        {"imgui"},
        {"video_capture"},
        {"swap_buffers"},
        {"frame_total"},
    }};
};

TelemetryState& telemetryState() {
    static TelemetryState state;
    return state;
}

std::string formatTimestamp(const char* pattern) {
    std::time_t now = std::time(nullptr);
    std::tm local_tm{};
    localtime_r(&now, &local_tm);
    char buffer[64] = {};
    if (std::strftime(buffer, sizeof(buffer), pattern, &local_tm) == 0) {
        return "00000000_000000";
    }
    return std::string(buffer);
}

std::string lineTimestamp() {
    return formatTimestamp("%Y-%m-%d %H:%M:%S");
}

std::string fileTimestamp() {
    return formatTimestamp("%Y%m%d_%H%M%S");
}

std::string joinPathString(const std::filesystem::path& path) {
    return path.lexically_normal().string();
}

void writeLine(const std::string& line) {
    TelemetryState& state = telemetryState();
    if (!state.enabled || !state.output.is_open()) {
        return;
    }
    state.output << '[' << lineTimestamp() << "] " << line << '\n';
    state.output.flush();
}

void echoTerminal(const std::string& line) {
    std::printf("[telemetry] %s\n", line.c_str());
}

std::string formatDouble(double value, int precision = 3) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

std::string formatFloat(float value, int precision = 3) {
    return formatDouble(static_cast<double>(value), precision);
}

GridDiagnostics inspectGrid(const GridData& grid) {
    GridDiagnostics diagnostics;
    if (grid.data.empty()) {
        return diagnostics;
    }

    diagnostics.min_value = std::numeric_limits<float>::max();
    diagnostics.max_value = std::numeric_limits<float>::lowest();
    double sum = 0.0;
    std::size_t finite_values = 0;
    for (float value : grid.data) {
        if (!std::isfinite(value)) {
            ++diagnostics.nonfinite_count;
            continue;
        }
        diagnostics.min_value = std::min(diagnostics.min_value, value);
        diagnostics.max_value = std::max(diagnostics.max_value, value);
        sum += static_cast<double>(value);
        ++finite_values;
    }

    if (finite_values == 0) {
        diagnostics.min_value = 0.0f;
        diagnostics.max_value = 0.0f;
        diagnostics.mean_value = 0.0;
    } else {
        diagnostics.mean_value = sum / static_cast<double>(finite_values);
    }

    return diagnostics;
}

double positionLimitForRegime(const Universe& universe) {
    if (universe.regime_index >= 4) {
        return RegimeConfig::STRUCT_BOX_SIZE_MPC * 2.5;
    }
    if (universe.regime_index == 0) {
        return 8.0;
    }
    return 256.0;
}

UniverseDiagnostics inspectUniverse(const Universe& universe) {
    UniverseDiagnostics diagnostics;
    diagnostics.flagged_active_particles = universe.particles.activeCount();
    diagnostics.density = inspectGrid(universe.density_field);
    diagnostics.ionization = inspectGrid(universe.ionization_field);
    diagnostics.emissivity = inspectGrid(universe.emissivity_field);

    const double position_limit = positionLimitForRegime(universe);
    bool bounds_initialized = false;

    const ParticlePool& particles = universe.particles;
    for (std::size_t i = 0; i < particles.x.size(); ++i) {
        if (!(particles.flags[i] & PF_ACTIVE)) {
            continue;
        }

        ++diagnostics.active_particles;

        const double px = particles.x[i];
        const double py = particles.y[i];
        const double pz = particles.z[i];
        const double vx = particles.vx[i];
        const double vy = particles.vy[i];
        const double vz = particles.vz[i];
        const double mass = particles.mass[i];

        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) {
            ++diagnostics.nonfinite_positions;
        }
        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) {
            ++diagnostics.nonfinite_velocities;
        }
        if (!std::isfinite(mass) || mass < 0.0) {
            ++diagnostics.negative_mass;
        }
        if (std::fabs(px) > position_limit || std::fabs(py) > position_limit || std::fabs(pz) > position_limit) {
            ++diagnostics.out_of_bounds;
        }

        if (!bounds_initialized) {
            diagnostics.min_x = diagnostics.max_x = px;
            diagnostics.min_y = diagnostics.max_y = py;
            diagnostics.min_z = diagnostics.max_z = pz;
            bounds_initialized = true;
        } else {
            diagnostics.min_x = std::min(diagnostics.min_x, px);
            diagnostics.max_x = std::max(diagnostics.max_x, px);
            diagnostics.min_y = std::min(diagnostics.min_y, py);
            diagnostics.max_y = std::max(diagnostics.max_y, py);
            diagnostics.min_z = std::min(diagnostics.min_z, pz);
            diagnostics.max_z = std::max(diagnostics.max_z, pz);
        }

        const double speed = std::sqrt(vx * vx + vy * vy + vz * vz);
        if (std::isfinite(speed)) {
            diagnostics.max_speed = std::max(diagnostics.max_speed, speed);
        }
        if (std::isfinite(mass) && mass > 0.0) {
            diagnostics.total_mass += mass;
        }

        switch (particles.type[i]) {
            case ParticleType::GAS: ++diagnostics.gas_particles; break;
            case ParticleType::DARK_MATTER: ++diagnostics.dark_matter_particles; break;
            case ParticleType::STAR: ++diagnostics.star_particles; break;
            case ParticleType::BLACKHOLE: ++diagnostics.black_holes; break;
            case ParticleType::PHOTON: ++diagnostics.photon_particles; break;
            case ParticleType::PROTON:
            case ParticleType::NEUTRON:
            case ParticleType::DEUTERIUM:
            case ParticleType::HELIUM3:
            case ParticleType::HELIUM4NUCLEI:
            case ParticleType::LITHIUM7:
                ++diagnostics.baryon_particles;
                break;
            default:
                break;
        }

        if (i < particles.ionized.size() && particles.ionized[i] != 0) {
            ++diagnostics.ionized_particles;
        }
        if (particles.flags[i] & PF_COLLAPSING) {
            ++diagnostics.collapsing_particles;
        }
        if (i < particles.star_state.size()) {
            if (particles.star_state[i] == StarState::PROTOSTAR) {
                ++diagnostics.protostars;
            }
            if (particles.star_state[i] == StarState::WHITE_DWARF ||
                particles.star_state[i] == StarState::NEUTRON_STAR ||
                particles.star_state[i] == StarState::BLACK_HOLE) {
                ++diagnostics.compact_remnants;
            }
        }
    }

    return diagnostics;
}

void updateSectionSummaries(const TelemetryFrameTimings& timings) {
    TelemetryState& state = telemetryState();
    state.sections[InputSection].addSample(timings.input_ms);
    state.sections[DeferredJumpSection].addSample(timings.deferred_jump_ms);
    state.sections[SimTotalSection].addSample(timings.sim_total_ms);
    state.sections[ClockStepSection].addSample(timings.clock_step_ms);
    state.sections[ManagerTickSection].addSample(timings.manager_tick_ms);
    state.sections[RegimeUpdateSection].addSample(timings.regime_update_ms);
    state.sections[AutoFrameSection].addSample(timings.auto_frame_ms);
    state.sections[RenderSetupSection].addSample(timings.render_setup_ms);
    state.sections[RenderSection].addSample(timings.render_ms);
    state.sections[ImGuiSection].addSample(timings.imgui_ms);
    state.sections[VideoCaptureSection].addSample(timings.video_capture_ms);
    state.sections[SwapBuffersSection].addSample(timings.swap_buffers_ms);
    state.sections[FrameTotalSection].addSample(timings.frame_ms);
}

std::string rendererSnapshotLine(const RendererDiagnostics& diagnostics) {
    std::ostringstream stream;
    stream << "renderer viewport=" << diagnostics.framebuffer_width << 'x' << diagnostics.framebuffer_height
           << " bloom=" << diagnostics.bloom_width << 'x' << diagnostics.bloom_height
           << " gpu_ms=" << formatFloat(diagnostics.last_gpu_ms)
           << " draws{particles=" << diagnostics.last_particle_draw_count
           << ",halos=" << diagnostics.last_halo_draw_count
           << ",volume_grid=" << diagnostics.last_volume_grid_nx << 'x'
           << diagnostics.last_volume_grid_ny << 'x' << diagnostics.last_volume_grid_nz << "}"
           << " shaders{particle=" << diagnostics.particle_shader_id
           << ",volume=" << diagnostics.volume_shader_id
           << ",inflation=" << diagnostics.inflation_shader_id
           << ",tonemap=" << diagnostics.tonemap_shader_id
           << ",bloom_threshold=" << diagnostics.bloom_threshold_shader_id
           << ",bloom_blur=" << diagnostics.bloom_blur_shader_id << "}"
           << " textures{hdr=" << diagnostics.hdr_color_tex_id
           << ",depth=" << diagnostics.hdr_depth_tex_id
           << ",bloom0=" << diagnostics.bloom_tex0_id
           << ",bloom1=" << diagnostics.bloom_tex1_id
           << ",density=" << diagnostics.density_tex_id << '@' << diagnostics.density_tex_nx << 'x'
           << diagnostics.density_tex_ny << 'x' << diagnostics.density_tex_nz
           << ",ionization=" << diagnostics.ionization_tex_id << '@' << diagnostics.ionization_tex_nx << 'x'
           << diagnostics.ionization_tex_ny << 'x' << diagnostics.ionization_tex_nz
           << ",emissivity=" << diagnostics.emissivity_tex_id << '@' << diagnostics.emissivity_tex_nx << 'x'
           << diagnostics.emissivity_tex_ny << 'x' << diagnostics.emissivity_tex_nz
           << ",inflation=" << diagnostics.inflation_tex_id << '@' << diagnostics.inflation_tex_width << 'x'
           << diagnostics.inflation_tex_height
           << ",pos_ssbo=" << diagnostics.particle_pos_ssbo_id
           << ",col_ssbo=" << diagnostics.particle_col_ssbo_id << '}';
    return stream.str();
}

std::string universeSnapshotLine(const Universe& universe, const UniverseDiagnostics& diagnostics) {
    std::ostringstream stream;
    stream << "regime=" << universe.regime_index
           << " cosmic_time=" << formatDouble(universe.cosmic_time, 6)
           << " scale_factor=" << formatDouble(universe.scale_factor, 6)
           << " temperature_keV=" << formatDouble(universe.temperature_keV, 6)
           << " particles{active=" << diagnostics.active_particles
           << ",flagged=" << diagnostics.flagged_active_particles
           << ",gas=" << diagnostics.gas_particles
           << ",dm=" << diagnostics.dark_matter_particles
           << ",stars=" << diagnostics.star_particles
           << ",bh=" << diagnostics.black_holes
           << ",photons=" << diagnostics.photon_particles
           << ",baryons=" << diagnostics.baryon_particles
           << ",ionized=" << diagnostics.ionized_particles
           << ",collapsing=" << diagnostics.collapsing_particles
           << ",protostars=" << diagnostics.protostars
           << ",compact=" << diagnostics.compact_remnants << "}"
           << " bounds{x=[" << formatDouble(diagnostics.min_x) << ',' << formatDouble(diagnostics.max_x)
           << "] y=[" << formatDouble(diagnostics.min_y) << ',' << formatDouble(diagnostics.max_y)
           << "] z=[" << formatDouble(diagnostics.min_z) << ',' << formatDouble(diagnostics.max_z)
           << "] max_speed=" << formatDouble(diagnostics.max_speed)
           << " total_mass=" << formatDouble(diagnostics.total_mass, 3) << "}"
           << " anomalies{nonfinite_pos=" << diagnostics.nonfinite_positions
           << ",nonfinite_vel=" << diagnostics.nonfinite_velocities
           << ",negative_mass=" << diagnostics.negative_mass
           << ",out_of_bounds=" << diagnostics.out_of_bounds << "}"
           << " fields{density[min=" << formatFloat(diagnostics.density.min_value)
           << ",max=" << formatFloat(diagnostics.density.max_value)
           << ",mean=" << formatDouble(diagnostics.density.mean_value)
           << ",nonfinite=" << diagnostics.density.nonfinite_count << "]"
           << " ionization[min=" << formatFloat(diagnostics.ionization.min_value)
           << ",max=" << formatFloat(diagnostics.ionization.max_value)
           << ",mean=" << formatDouble(diagnostics.ionization.mean_value)
           << ",nonfinite=" << diagnostics.ionization.nonfinite_count << "]"
           << " emissivity[min=" << formatFloat(diagnostics.emissivity.min_value)
           << ",max=" << formatFloat(diagnostics.emissivity.max_value)
           << ",mean=" << formatDouble(diagnostics.emissivity.mean_value)
           << ",nonfinite=" << diagnostics.emissivity.nonfinite_count << "]}";
    return stream.str();
}

std::string timingsLine(const TelemetryFrameTimings& timings) {
    std::ostringstream stream;
    stream << "timings_ms{input=" << formatDouble(timings.input_ms)
           << ",deferred_jump=" << formatDouble(timings.deferred_jump_ms)
           << ",sim_total=" << formatDouble(timings.sim_total_ms)
           << ",clock_step=" << formatDouble(timings.clock_step_ms)
           << ",manager_tick=" << formatDouble(timings.manager_tick_ms)
           << ",regime_update=" << formatDouble(timings.regime_update_ms)
           << ",auto_frame=" << formatDouble(timings.auto_frame_ms)
           << ",render_setup=" << formatDouble(timings.render_setup_ms)
           << ",render=" << formatDouble(timings.render_ms)
           << ",imgui=" << formatDouble(timings.imgui_ms)
           << ",video_capture=" << formatDouble(timings.video_capture_ms)
           << ",swap_buffers=" << formatDouble(timings.swap_buffers_ms)
           << ",frame=" << formatDouble(timings.frame_ms) << '}';
    return stream.str();
}

}  // namespace

bool TelemetrySession::start(const std::filesystem::path& project_root,
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
                             const RendererDiagnostics& renderer_diagnostics) {
    TelemetryState& state = telemetryState();

    std::filesystem::path output_path = requested_output_path;
    if (output_path.empty()) {
        output_path = project_root / "logs" / ("cosmos_telemetry_" + fileTimestamp() + ".log");
    } else if (output_path.is_relative()) {
        output_path = project_root / output_path;
    }

    std::error_code ec;
    std::filesystem::create_directories(output_path.parent_path(), ec);
    state.output.open(output_path, std::ios::out | std::ios::trunc);
    if (!state.output.is_open()) {
        std::fprintf(stderr,
                     "[telemetry] Failed to open log file '%s'\n",
                     output_path.string().c_str());
        return false;
    }

    state.enabled = true;
    state.output_path = output_path;
    enabled_ = true;
    output_path_ = output_path;

    std::ostringstream header;
    header << "session start build_quality=" << (build_quality ? build_quality : "unknown")
           << " seed=" << seed
           << " window=" << window_width << 'x' << window_height
           << " video_enabled=" << (video_enabled ? "yes" : "no")
           << " panoramic_camera=" << (panoramic_camera ? "yes" : "no")
           << " gl_version='" << (gl_version ? gl_version : "unknown") << "'"
           << " gl_renderer='" << (gl_renderer ? gl_renderer : "unknown") << "'"
           << " gl_vendor='" << (gl_vendor ? gl_vendor : "unknown") << "'"
           << " output='" << joinPathString(output_path) << "'";
    writeLine(header.str());
    writeLine(rendererSnapshotLine(renderer_diagnostics));
    echoTerminal("logging enabled -> " + joinPathString(output_path));
    return true;
}

void TelemetrySession::noteCheckpoint(const std::string& label,
                                      const Universe& universe,
                                      const RendererDiagnostics& renderer_diagnostics) {
    if (!enabled_) {
        return;
    }
    const UniverseDiagnostics diagnostics = inspectUniverse(universe);
    writeLine("checkpoint label='" + label + "' " + universeSnapshotLine(universe, diagnostics));
    writeLine(rendererSnapshotLine(renderer_diagnostics));
}

void TelemetrySession::noteOperatorEvent(const std::string& event) {
    if (!enabled_) {
        return;
    }
    writeLine("operator_event " + event);
    echoTerminal("event -> " + event);
}

void TelemetrySession::noteRegimeChange(int previous_regime,
                                        int current_regime,
                                        const Universe& universe) {
    if (!enabled_) {
        return;
    }
    std::ostringstream stream;
    stream << "regime_change from=" << previous_regime
           << " to=" << current_regime
           << " cosmic_time=" << formatDouble(universe.cosmic_time, 6)
           << " scale_factor=" << formatDouble(universe.scale_factor, 6)
           << " temperature_keV=" << formatDouble(universe.temperature_keV, 6);
    const std::string line = stream.str();
    writeLine(line);
    echoTerminal(line);
}

void TelemetrySession::noteShaderReload(const RendererDiagnostics& renderer_diagnostics) {
    if (!enabled_) {
        return;
    }
    writeLine("shader_reload " + rendererSnapshotLine(renderer_diagnostics));
    echoTerminal("shader reload captured");
}

void TelemetrySession::recordFrame(std::uint64_t frame_index,
                                   int sim_steps,
                                   float real_dt,
                                   const Universe& universe,
                                   const RendererDiagnostics& renderer_diagnostics,
                                   const TelemetryFrameTimings& timings) {
    if (!enabled_) {
        return;
    }

    updateSectionSummaries(timings);

    const bool emit_frame_sample = (frame_index % kFrameSampleStride) == 0;
    const bool emit_terminal_summary = (frame_index % kTerminalSampleStride) == 0;
    UniverseDiagnostics diagnostics;
    const UniverseDiagnostics* diagnostics_ptr = nullptr;
    if (emit_frame_sample || emit_terminal_summary) {
        diagnostics = inspectUniverse(universe);
        diagnostics_ptr = &diagnostics;
    }

    if (emit_frame_sample && diagnostics_ptr) {
        std::ostringstream stream;
        stream << "frame index=" << frame_index
               << " real_dt=" << formatDouble(real_dt, 6)
               << " sim_steps=" << sim_steps
               << " fps=" << formatFloat(universe.fps)
               << " gpu_ms=" << formatFloat(universe.gpu_time_ms)
               << ' ' << timingsLine(timings)
               << ' ' << universeSnapshotLine(universe, *diagnostics_ptr)
               << ' ' << rendererSnapshotLine(renderer_diagnostics);
        writeLine(stream.str());
    }

    if (emit_terminal_summary) {
        std::ostringstream stream;
        stream << "frame=" << frame_index
               << " regime=" << universe.regime_index
               << " fps=" << formatFloat(universe.fps, 2)
               << " frame_ms=" << formatDouble(timings.frame_ms, 2)
               << " sim_ms=" << formatDouble(timings.sim_total_ms, 2)
               << " render_ms=" << formatDouble(timings.render_ms, 2)
               << " gpu_ms=" << formatFloat(universe.gpu_time_ms, 2)
               << " active=" << (diagnostics_ptr ? diagnostics_ptr->active_particles : 0)
               << " anomalies=" << ((diagnostics_ptr && diagnostics_ptr->hasAnomaly()) ? "yes" : "no");
        echoTerminal(stream.str());
    }
}

void TelemetrySession::finish(int exit_code,
                              const Universe& universe,
                              const RendererDiagnostics& renderer_diagnostics) {
    if (!enabled_) {
        return;
    }

    const UniverseDiagnostics diagnostics = inspectUniverse(universe);
    writeLine("shutdown exit_code=" + std::to_string(exit_code) + ' ' + universeSnapshotLine(universe, diagnostics));
    writeLine(rendererSnapshotLine(renderer_diagnostics));

    const TelemetryState& state = telemetryState();
    for (const SectionSummary& section : state.sections) {
        std::ostringstream stream;
        stream << "summary section=" << section.label
               << " samples=" << section.samples
               << " avg_ms=" << formatDouble(section.averageMs())
               << " min_ms=" << formatDouble(section.samples > 0 ? section.min_ms : 0.0)
               << " max_ms=" << formatDouble(section.max_ms)
               << " total_ms=" << formatDouble(section.total_ms);
        writeLine(stream.str());
    }

    writeLine("session end output='" + joinPathString(output_path_) + "'");
    telemetryState().output.close();
    echoTerminal("log saved -> " + joinPathString(output_path_));
}