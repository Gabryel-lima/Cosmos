// src/main.cpp — Ponto de entrada da simulação cósmica.
// Inicializa GLFW + OpenGL 4.3 + ImGui, depois executa o loop principal.

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/CosmicClock.hpp"
#include "core/RegimeManager.hpp"
#include "core/SimulationRandom.hpp"
#include "core/Telemetry.hpp"
#include "core/Universe.hpp"
#include "core/Camera.hpp"
#include "render/Renderer.hpp"
#include "render/RegimeOverlay.hpp"
#include "render/ICosmicRenderer.hpp"
#include "physics/Constants.hpp"
#include "physics/ParticlePool.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <algorithm>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

// ── Estado global (apenas na unidade de tradução principal) ──────────────────────────────

// ── Debug helpers ──────────────────────────────────────────────────────────────────────
#define DBGF(fmt, ...) do { std::printf(fmt, ##__VA_ARGS__); std::fflush(stdout); } while(0)
static bool dbgVerboseFrame(std::uint64_t f) { return f < 5 || f % 60 == 0; }

static int g_width  = 1280;
static int g_height = 720;
static bool g_fullscreen    = false;
static bool g_show_hud      = true;
static bool g_reload_shaders = false;
static std::string g_imgui_ini_path = "imgui.ini";
static std::filesystem::path g_project_root_path;

struct TelemetryConfig {
    bool enabled = false;
    std::filesystem::path output_path;
};

static TelemetryConfig g_telemetry_config;

struct VideoExportConfig {
    bool enabled = false;
    bool panoramic_camera = false;
    std::string panoramic_preset = "cinematic";
    PanoramicAutoFrameSettings panoramic_settings;
    int capture_fps = 30;
    int output_fps = 60;
    std::filesystem::path output_path = "cosmos_video.mp4";
};

static VideoExportConfig g_video_export;

struct PanoramicSettingsOverrides {
    std::optional<double> orbit_speed;
    std::optional<double> distance_multiplier;
    std::optional<double> zoom_multiplier;
    std::optional<double> elevation_bias;
    std::optional<double> elevation_amplitude;
    std::optional<double> target_lift_scale;
    std::optional<double> lateral_sway_scale;
    std::optional<double> smoothing;
};

static bool parseDoubleArg(const char* value, double& out) {
    if (!value) return false;
    char* end = nullptr;
    out = std::strtod(value, &end);
    return end && *end == '\0' && std::isfinite(out);
}

static bool isTruthyEnvValue(const char* value) {
    if (!value || !*value) {
        return false;
    }
    return std::strcmp(value, "0") != 0 &&
           std::strcmp(value, "false") != 0 &&
           std::strcmp(value, "FALSE") != 0 &&
           std::strcmp(value, "off") != 0 &&
           std::strcmp(value, "OFF") != 0 &&
           std::strcmp(value, "no") != 0 &&
           std::strcmp(value, "NO") != 0;
}

static double elapsedMs(const std::chrono::steady_clock::time_point& start,
                        const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

static PanoramicAutoFrameSettings makePanoramicPreset(const std::string& preset_name) {
    PanoramicAutoFrameSettings settings;
    if (preset_name == "gentle") {
        settings.orbit_speed = 0.65;
        settings.distance_multiplier = 1.20;
        settings.zoom_multiplier = 0.95;
        settings.elevation_bias = 0.18;
        settings.elevation_amplitude = 0.08;
        settings.target_lift_scale = 0.10;
        settings.lateral_sway_scale = 0.04;
        settings.smoothing = 2.0;
        settings.phase_frequency = 0.16;
        settings.regime_phase_boost = 0.010;
        settings.elevation_frequency = 0.18;
        settings.distance_frequency = 0.14;
        settings.target_lift_frequency = 0.12;
        settings.lateral_sway_frequency = 0.22;
    } else if (preset_name == "orbit") {
        settings.orbit_speed = 1.10;
        settings.distance_multiplier = 1.05;
        settings.zoom_multiplier = 1.05;
        settings.elevation_bias = 0.24;
        settings.elevation_amplitude = 0.10;
        settings.target_lift_scale = 0.12;
        settings.lateral_sway_scale = 0.03;
        settings.smoothing = 2.8;
        settings.phase_frequency = 0.28;
        settings.regime_phase_boost = 0.020;
        settings.elevation_frequency = 0.24;
        settings.distance_frequency = 0.18;
        settings.target_lift_frequency = 0.16;
        settings.lateral_sway_frequency = 0.20;
    } else if (preset_name == "flyby") {
        settings.orbit_speed = 1.45;
        settings.distance_multiplier = 0.88;
        settings.zoom_multiplier = 1.22;
        settings.elevation_bias = 0.14;
        settings.elevation_amplitude = 0.22;
        settings.target_lift_scale = 0.20;
        settings.lateral_sway_scale = 0.16;
        settings.smoothing = 3.2;
        settings.phase_frequency = 0.20;
        settings.regime_phase_boost = 0.022;
        settings.elevation_frequency = 0.36;
        settings.distance_frequency = 0.31;
        settings.target_lift_frequency = 0.24;
        settings.lateral_sway_frequency = 0.58;
    } else if (preset_name == "inspect") {
        settings.orbit_speed = 0.85;
        settings.distance_multiplier = 0.72;
        settings.zoom_multiplier = 1.55;
        settings.elevation_bias = 0.28;
        settings.elevation_amplitude = 0.14;
        settings.target_lift_scale = 0.08;
        settings.lateral_sway_scale = 0.05;
        settings.smoothing = 3.6;
        settings.phase_frequency = 0.12;
        settings.regime_phase_boost = 0.008;
        settings.elevation_frequency = 0.16;
        settings.distance_frequency = 0.12;
        settings.target_lift_frequency = 0.10;
        settings.lateral_sway_frequency = 0.16;
    }
    return settings;
}

static bool isKnownPanoramicPreset(const std::string& preset_name) {
    return preset_name == "cinematic" ||
           preset_name == "gentle" ||
           preset_name == "orbit" ||
           preset_name == "flyby" ||
           preset_name == "inspect";
}

static PanoramicAutoFrameSettings resolvePanoramicSettings(const std::string& preset_name,
                                                           const PanoramicSettingsOverrides& overrides) {
    PanoramicAutoFrameSettings settings = makePanoramicPreset(preset_name);
    if (overrides.orbit_speed) settings.orbit_speed = *overrides.orbit_speed;
    if (overrides.distance_multiplier) settings.distance_multiplier = *overrides.distance_multiplier;
    if (overrides.zoom_multiplier) settings.zoom_multiplier = *overrides.zoom_multiplier;
    if (overrides.elevation_bias) settings.elevation_bias = *overrides.elevation_bias;
    if (overrides.elevation_amplitude) settings.elevation_amplitude = *overrides.elevation_amplitude;
    if (overrides.target_lift_scale) settings.target_lift_scale = *overrides.target_lift_scale;
    if (overrides.lateral_sway_scale) settings.lateral_sway_scale = *overrides.lateral_sway_scale;
    if (overrides.smoothing) settings.smoothing = *overrides.smoothing;
    return settings;
}

static std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

class VideoExporter {
public:
    bool start(const VideoExportConfig& config, int width, int height) {
        if (!config.enabled) {
            return true;
        }
        if (width <= 0 || height <= 0 || config.capture_fps <= 0 || config.output_fps <= 0) {
            std::fprintf(stderr, "[video] Invalid export settings. width=%d height=%d capture_fps=%d output_fps=%d\n",
                         width, height, config.capture_fps, config.output_fps);
            return false;
        }

        width_ = width;
        height_ = height;
        capture_fps_ = config.capture_fps;
        output_fps_ = config.output_fps;
        output_path_ = config.output_path;
        frame_bytes_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 3u);

        std::string filter = "vflip";
        if (output_fps_ > capture_fps_) {
            filter += ",minterpolate=fps=" + std::to_string(output_fps_) + ":mi_mode=mci:mc_mode=aobmc:vsbmc=1";
        } else if (output_fps_ != capture_fps_) {
            filter += ",fps=" + std::to_string(output_fps_);
        }

        const std::string command =
            "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size "
            + std::to_string(width_) + "x" + std::to_string(height_)
            + " -framerate " + std::to_string(capture_fps_)
            + " -i - -an -vf " + shellQuote(filter)
            + " -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p "
            + shellQuote(output_path_.string());

        pipe_ = popen(command.c_str(), "w");
        if (!pipe_) {
            std::fprintf(stderr, "[video] Failed to start ffmpeg. Is it installed and on PATH?\n");
            return false;
        }

        std::printf("[video] Recording %dx%d at %d fps -> %d fps interpolated output: %s\n",
                    width_, height_, capture_fps_, output_fps_, output_path_.string().c_str());
        return true;
    }

    bool isActive() const {
        return pipe_ != nullptr;
    }

    bool captureFrame() {
        if (!pipe_) {
            return true;
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, frame_bytes_.data());

        const std::size_t expected = frame_bytes_.size();
        const std::size_t written = std::fwrite(frame_bytes_.data(), 1, expected, pipe_);
        if (written != expected) {
            std::fprintf(stderr, "[video] Failed to write frame %zu to ffmpeg pipe.\n", frame_index_);
            return false;
        }

        ++frame_index_;
        return true;
    }

    bool finish() {
        if (!pipe_) {
            return true;
        }

        const int status = pclose(pipe_);
        pipe_ = nullptr;
        if (status != 0) {
            std::fprintf(stderr, "[video] ffmpeg exited with status %d while finalizing %s\n",
                         status, output_path_.string().c_str());
            return false;
        }

        std::printf("[video] Export finished: %s (%zu frames captured)\n",
                    output_path_.string().c_str(), frame_index_);
        return true;
    }

private:
    FILE* pipe_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int capture_fps_ = 0;
    int output_fps_ = 0;
    std::size_t frame_index_ = 0;
    std::filesystem::path output_path_;
    std::vector<unsigned char> frame_bytes_;
};

struct AppState {
    GLFWwindow*    window   = nullptr;
    CosmicClock    clock;
    RegimeManager  mgr;
    Universe       universe;
    Camera         camera;
    Renderer       renderer;
    RegimeOverlay  overlay;
    TelemetrySession telemetry;
    std::uint64_t frame_index = 0;
    int            deferred_jump_regime = -1;
    bool           running  = true;
};

static void recenterCameraToScene(AppState& app, int regime_index) {
    SceneFrame scene_frame = Camera::estimateSceneFrame(app.universe);
    app.camera.applyState(app.camera.getSceneFittedState(regime_index, scene_frame));
}

static void jumpToRegimeAndFrame(AppState& app, int regime_index) {
    app.mgr.jumpToRegime(regime_index, app.clock, app.universe);
    recenterCameraToScene(app, regime_index);
    app.universe.active_particles = static_cast<int>(app.universe.particles.activeCount());
    if (app.telemetry.enabled()) {
        app.telemetry.noteOperatorEvent("jump_applied regime=" + std::to_string(regime_index));
        app.telemetry.noteCheckpoint("regime_jump", app.universe, app.renderer.collectDiagnostics());
    }
}

static void requestDeferredRegimeJump(AppState& app, int regime_index) {
    app.deferred_jump_regime = CosmicClock::clampRegimeIndex(regime_index);
    if (app.telemetry.enabled()) {
        app.telemetry.noteOperatorEvent("jump_requested regime=" + std::to_string(app.deferred_jump_regime));
    }
}

static void toggleNearestTracking(AppState& app) {
    if (app.camera.tracked_id != std::numeric_limits<uint32_t>::max()) {
        app.camera.releaseTracking();
        std::printf("[Camera] Tracking released\n");
        return;
    }

    const ParticlePool& pp = app.universe.particles;
    uint32_t best_id = std::numeric_limits<uint32_t>::max();
    double best_r2 = 1e300;
    auto cam_pos = app.camera.position;
    for (size_t i = 0; i < pp.x.size(); ++i) {
        if (!(pp.flags[i] & PF_ACTIVE)) continue;
        bool trackable = (pp.type[i] == ParticleType::STAR   ||
                          pp.type[i] == ParticleType::BLACKHOLE ||
                          pp.type[i] == ParticleType::PROTON ||
                          pp.type[i] == ParticleType::NEUTRON ||
                          pp.type[i] == ParticleType::DEUTERIUM ||
                          pp.type[i] == ParticleType::HELIUM4NUCLEI ||
                          pp.type[i] == ParticleType::ELECTRON ||
                          pp.type[i] == ParticleType::GAS ||
                          pp.type[i] == ParticleType::DARK_MATTER);
        if (!trackable) continue;
        double dx = pp.x[i] - cam_pos.x;
        double dy = pp.y[i] - cam_pos.y;
        double dz = pp.z[i] - cam_pos.z;
        double r2 = dx*dx + dy*dy + dz*dz;
        if (r2 < best_r2) {
            best_r2 = r2;
            best_id = static_cast<uint32_t>(i);
        }
    }

    if (best_id != std::numeric_limits<uint32_t>::max()) {
        app.camera.trackParticle(best_id);
        std::printf("[Camera] Tracking particle %u\n", best_id);
        if (app.telemetry.enabled()) {
            app.telemetry.noteOperatorEvent("tracking particle=" + std::to_string(best_id));
        }
    } else {
        std::printf("[Camera] No trackable particles found\n");
        if (app.telemetry.enabled()) {
            app.telemetry.noteOperatorEvent("tracking_failed no_trackable_particle");
        }
    }
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

static AppState* g_app = nullptr;

static void on_framebuffer_resize(GLFWwindow* /*w*/, int width, int height) {
    if (width == 0 || height == 0) return;
    g_width  = width;
    g_height = height;
    if (g_app) {
        g_app->renderer.resize(width, height);
        if (g_app->telemetry.enabled()) {
            g_app->telemetry.noteOperatorEvent("resize width=" + std::to_string(width) +
                                               " height=" + std::to_string(height));
        }
    }
}

static void on_key(GLFWwindow* /*w*/, int key, int /*sc*/, int action, int mods) {
    if (!g_app) return;
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    if (action == GLFW_PRESS) {
        switch (key) {
        case GLFW_KEY_ESCAPE:
            if (g_app->camera.tracked_id != std::numeric_limits<uint32_t>::max()) {
                g_app->camera.releaseTracking();
                std::printf("[Camera] Tracking released\n");
                if (g_app->telemetry.enabled()) {
                    g_app->telemetry.noteOperatorEvent("tracking_released key=ESC");
                }
            } else {
                g_app->running = false;
                if (g_app->telemetry.enabled()) {
                    g_app->telemetry.noteOperatorEvent("quit_requested key=ESC");
                }
            }
            break;

        case GLFW_KEY_Q:
            if (mods & GLFW_MOD_CONTROL) {
                g_app->running = false;
                if (g_app->telemetry.enabled()) {
                    g_app->telemetry.noteOperatorEvent("quit_requested key=CTRL+Q");
                }
            }
            break;

        case GLFW_KEY_SPACE:
            if (g_app->clock.isPaused()) g_app->clock.play();
            else                         g_app->clock.pause();
            if (g_app->telemetry.enabled()) {
                g_app->telemetry.noteOperatorEvent(std::string("clock_") +
                                                   (g_app->clock.isPaused() ? "paused" : "running"));
            }
            break;

        case GLFW_KEY_H:
            g_show_hud = !g_show_hud;
            g_app->overlay.visible = g_show_hud;
            if (g_app->telemetry.enabled()) {
                g_app->telemetry.noteOperatorEvent(std::string("hud visible=") + (g_show_hud ? "yes" : "no"));
            }
            break;

        case GLFW_KEY_R:
            g_reload_shaders = true;
            if (g_app->telemetry.enabled()) {
                g_app->telemetry.noteOperatorEvent("shader_reload_requested");
            }
            break;

        case GLFW_KEY_F: {
            // Alternar tela cheia
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(mon);
            if (!g_fullscreen) {
                glfwSetWindowMonitor(g_app->window, mon, 0, 0,
                                     mode->width, mode->height,
                                     mode->refreshRate);
                g_fullscreen = true;
            } else {
                glfwSetWindowMonitor(g_app->window, nullptr,
                                     100, 100, 1280, 720, 0);
                g_fullscreen = false;
            }
            if (g_app->telemetry.enabled()) {
                g_app->telemetry.noteOperatorEvent(std::string("fullscreen enabled=") +
                                                   (g_fullscreen ? "yes" : "no"));
            }
            break;
        }

        case GLFW_KEY_1: requestDeferredRegimeJump(*g_app, 0); break;
        case GLFW_KEY_2: requestDeferredRegimeJump(*g_app, 1); break;
        case GLFW_KEY_3: requestDeferredRegimeJump(*g_app, 2); break;
        case GLFW_KEY_4: requestDeferredRegimeJump(*g_app, 3); break;
        case GLFW_KEY_5: requestDeferredRegimeJump(*g_app, 4); break;
        case GLFW_KEY_6: requestDeferredRegimeJump(*g_app, 5); break;
        case GLFW_KEY_7: requestDeferredRegimeJump(*g_app, 6); break;
        case GLFW_KEY_8: requestDeferredRegimeJump(*g_app, 7); break;
        case GLFW_KEY_9: requestDeferredRegimeJump(*g_app, 8); break;

        case GLFW_KEY_C:
            recenterCameraToScene(*g_app, g_app->mgr.getCurrentRegimeIndex());
            std::printf("[Camera] Re-centered on scene for regime %d\n", g_app->mgr.getCurrentRegimeIndex());
            if (g_app->telemetry.enabled()) {
                g_app->telemetry.noteOperatorEvent("camera_recenter regime=" +
                                                   std::to_string(g_app->mgr.getCurrentRegimeIndex()));
            }
            break;

        case GLFW_KEY_T:
            toggleNearestTracking(*g_app);
            break;
        }

        // Ciclar modo da câmera via Tab
        if (key == GLFW_KEY_TAB && (mods & GLFW_MOD_SHIFT) == 0) {
            g_app->camera.updateMode();
            if (g_app->telemetry.enabled()) {
                g_app->telemetry.noteOperatorEvent("camera_mode_cycle");
            }
        }
    }
}

static void on_char(GLFWwindow* /*w*/, unsigned int codepoint) {
    if (!g_app) return;
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    switch (codepoint) {
    case '.':
    case '>':
        g_app->clock.stepSingleFrame();
        if (g_app->telemetry.enabled()) {
            g_app->telemetry.noteOperatorEvent("clock_single_frame_step");
        }
        break;

    case ',':
    case '<':
    case '[':
    case '{': {
        double scale = g_app->clock.getTimeScale();
        g_app->clock.setTimeScale(scale * 2.0);
        if (g_app->telemetry.enabled()) {
            g_app->telemetry.noteOperatorEvent("time_scale value=" + std::to_string(g_app->clock.getTimeScale()));
        }
        break;
    }

    case ';':
    case ':':
    case ']':
    case '}': {
        double scale = g_app->clock.getTimeScale();
        g_app->clock.setTimeScale(scale * 0.5);
        if (g_app->telemetry.enabled()) {
            g_app->telemetry.noteOperatorEvent("time_scale value=" + std::to_string(g_app->clock.getTimeScale()));
        }
        break;
    }

    default:
        break;
    }
}

static void on_scroll(GLFWwindow* /*w*/, double /*dx*/, double dy) {
    if (!g_app || ImGui::GetIO().WantCaptureMouse) return;
    g_app->camera.processScroll(static_cast<float>(dy));
}

static double g_last_mouse_x = 0.0, g_last_mouse_y = 0.0;
static bool   g_first_mouse  = true;

static void on_cursor_pos(GLFWwindow* /*w*/, double xpos, double ypos) {
    if (!g_app) return;
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (g_first_mouse) {
        g_last_mouse_x = xpos;
        g_last_mouse_y = ypos;
        g_first_mouse  = false;
    }
    double dx = xpos - g_last_mouse_x;
    double dy = g_last_mouse_y - ypos;  // Y invertido
    g_last_mouse_x = xpos;
    g_last_mouse_y = ypos;

    // Rotacionar apenas com botão esquerdo do mouse pressionado
    if (glfwGetMouseButton(g_app->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        // Inverter direção do movimento enquanto o botão esquerdo estiver pressionado
        g_app->camera.processMouseDelta(static_cast<float>(-dx),
                                         static_cast<float>(dy));
    }
}

// ── Inicialização ───────────────────────────────────────────────────────────────────

static bool parseArgs(int argc, char** argv) {
    g_telemetry_config.enabled = isTruthyEnvValue(std::getenv("COSMOS_LOG"));
    if (const char* env_output = std::getenv("COSMOS_LOG_OUTPUT")) {
        g_telemetry_config.output_path = env_output;
    }

    PanoramicSettingsOverrides panoramic_overrides;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--fullscreen" || arg == "-f") g_fullscreen = true;
        if (arg == "--width"  && i+1 < argc) g_width  = std::atoi(argv[++i]);
        if (arg == "--height" && i+1 < argc) g_height = std::atoi(argv[++i]);
        if (arg == "--seed" && i + 1 < argc) {
            unsigned long parsed_seed = std::strtoul(argv[++i], nullptr, 10);
            simrng::setGlobalSeed(static_cast<std::uint32_t>(parsed_seed));
        }
        if (arg == "--video") {
            g_video_export.enabled = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                g_video_export.output_path = argv[++i];
            }
        }
        if (arg == "--video-panorama" || arg == "--video-autocam") {
            g_video_export.enabled = true;
            g_video_export.panoramic_camera = true;
        }
        if (arg == "--video-panorama-preset" && i + 1 < argc) {
            g_video_export.enabled = true;
            g_video_export.panoramic_camera = true;
            g_video_export.panoramic_preset = argv[++i];
        }
        if (arg == "--video-capture-fps" && i + 1 < argc) {
            g_video_export.enabled = true;
            g_video_export.capture_fps = std::max(1, std::atoi(argv[++i]));
        }
        if (arg == "--video-fps" && i + 1 < argc) {
            g_video_export.enabled = true;
            g_video_export.output_fps = std::max(1, std::atoi(argv[++i]));
        }
        if (arg == "--log") {
            g_telemetry_config.enabled = true;
        }
        if (arg == "--log-output" && i + 1 < argc) {
            g_telemetry_config.enabled = true;
            g_telemetry_config.output_path = argv[++i];
        }
        if ((arg == "--video-panorama-speed" || arg == "--video-autocam-speed") && i + 1 < argc) {
            double value = 0.0;
            if (parseDoubleArg(argv[++i], value)) {
                g_video_export.enabled = true;
                g_video_export.panoramic_camera = true;
                panoramic_overrides.orbit_speed = value;
            }
        }
        if ((arg == "--video-panorama-distance" || arg == "--video-autocam-distance") && i + 1 < argc) {
            double value = 0.0;
            if (parseDoubleArg(argv[++i], value)) {
                g_video_export.enabled = true;
                g_video_export.panoramic_camera = true;
                panoramic_overrides.distance_multiplier = value;
            }
        }
        if ((arg == "--video-panorama-zoom" || arg == "--video-autocam-zoom") && i + 1 < argc) {
            double value = 0.0;
            if (parseDoubleArg(argv[++i], value)) {
                g_video_export.enabled = true;
                g_video_export.panoramic_camera = true;
                panoramic_overrides.zoom_multiplier = value;
            }
        }
        if ((arg == "--video-panorama-height" || arg == "--video-autocam-height") && i + 1 < argc) {
            double value = 0.0;
            if (parseDoubleArg(argv[++i], value)) {
                g_video_export.enabled = true;
                g_video_export.panoramic_camera = true;
                panoramic_overrides.target_lift_scale = value;
            }
        }
        if ((arg == "--video-panorama-sway" || arg == "--video-autocam-sway") && i + 1 < argc) {
            double value = 0.0;
            if (parseDoubleArg(argv[++i], value)) {
                g_video_export.enabled = true;
                g_video_export.panoramic_camera = true;
                panoramic_overrides.lateral_sway_scale = value;
            }
        }
        if ((arg == "--video-panorama-smooth" || arg == "--video-autocam-smooth") && i + 1 < argc) {
            double value = 0.0;
            if (parseDoubleArg(argv[++i], value)) {
                g_video_export.enabled = true;
                g_video_export.panoramic_camera = true;
                panoramic_overrides.smoothing = value;
            }
        }
    }
    if (!isKnownPanoramicPreset(g_video_export.panoramic_preset)) {
        std::fprintf(stderr,
                     "[video] Unknown panoramic preset '%s'. Falling back to 'cinematic'. Available presets: cinematic, gentle, orbit, flyby, inspect\n",
                     g_video_export.panoramic_preset.c_str());
        g_video_export.panoramic_preset = "cinematic";
    }
    g_video_export.panoramic_settings = resolvePanoramicSettings(g_video_export.panoramic_preset,
                                                                 panoramic_overrides);
    return true;
}

static std::filesystem::path resolveExecutablePath(const char* argv0) {
    #ifdef __linux__
        std::vector<char> buffer(4096, '\0');
        const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (length > 0) {
            buffer[static_cast<size_t>(length)] = '\0';
            return std::filesystem::path(buffer.data());
        }
    #endif

    if (argv0 && *argv0) {
        std::error_code ec;
        std::filesystem::path candidate(argv0);
        if (candidate.is_relative()) {
            candidate = std::filesystem::current_path(ec) / candidate;
        }
        std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, ec);
        if (!ec) {
            return normalized;
        }
        return candidate;
    }

    return std::filesystem::current_path();
}

static std::filesystem::path resolveLayoutIniPath(const std::filesystem::path& executable_dir) {
    std::error_code ec;
    for (std::filesystem::path probe = executable_dir; !probe.empty(); ) {
        if (std::filesystem::is_regular_file(probe / "CMakeLists.txt", ec)) {
            return probe / "imgui.ini";
        }
        ec.clear();
        const std::filesystem::path parent = probe.parent_path();
        if (parent.empty() || parent == probe) {
            break;
        }
        probe = parent;
    }
    return executable_dir / "imgui.ini";
}

static std::filesystem::path resolveProjectRoot(const std::filesystem::path& executable_dir) {
    std::error_code ec;
    for (std::filesystem::path probe = executable_dir; !probe.empty(); ) {
        if (std::filesystem::is_regular_file(probe / "CMakeLists.txt", ec)) {
            return probe;
        }
        ec.clear();
        const std::filesystem::path parent = probe.parent_path();
        if (parent.empty() || parent == probe) {
            break;
        }
        probe = parent;
    }
    return executable_dir;
}

static void configureRuntimePaths(const char* argv0) {
    std::error_code ec;
    const std::filesystem::path executable_path = resolveExecutablePath(argv0);
    const std::filesystem::path executable_dir = executable_path.has_parent_path()
        ? executable_path.parent_path()
        : std::filesystem::current_path(ec);

    if (!executable_dir.empty()) {
        std::filesystem::current_path(executable_dir, ec);
        if (ec) {
            std::fprintf(stderr,
                         "[main] Failed to switch cwd to executable dir '%s': %s\n",
                         executable_dir.string().c_str(),
                         ec.message().c_str());
            ec.clear();
        }

        g_project_root_path = resolveProjectRoot(executable_dir);
        g_imgui_ini_path = resolveLayoutIniPath(executable_dir).string();
        std::printf("[main] Runtime directory: %s\n", executable_dir.string().c_str());
        std::printf("[main] Project root: %s\n", g_project_root_path.string().c_str());
        std::printf("[main] ImGui layout file: %s\n", g_imgui_ini_path.c_str());
    }
}

static bool initGLFW(AppState& app) {
    if (!glfwInit()) {
        std::fprintf(stderr, "[main] glfwInit() failed.\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif
    glfwWindowHint(GLFW_SAMPLES, 4);  // MSAA ×4 (suavização de bordas)

    GLFWmonitor* mon = g_fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    app.window = glfwCreateWindow(g_width, g_height, "Cosmos", mon, nullptr);
    if (!app.window) {
        std::fprintf(stderr, "[main] Failed to create GLFW window.\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(app.window);
    glfwSwapInterval(g_video_export.enabled ? 0 : 1);  // Exportação não deve esperar V-Sync

    // Carregar OpenGL
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::fprintf(stderr, "[main] Failed to load OpenGL via GLAD.\n");
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return false;
    }

    std::printf("[main] OpenGL %s on %s\n",
                glGetString(GL_VERSION), glGetString(GL_RENDERER));

    // Registrar callbacks
    glfwSetFramebufferSizeCallback(app.window, on_framebuffer_resize);
    glfwSetKeyCallback(app.window, on_key);
    glfwSetCharCallback(app.window, on_char);
    glfwSetScrollCallback(app.window, on_scroll);
    glfwSetCursorPosCallback(app.window, on_cursor_pos);

    return true;
}

static void initImGui(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = g_imgui_ini_path.c_str();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");
}

// ── Principal ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    parseArgs(argc, argv);
    configureRuntimePaths(argv[0]);

    AppState app;
    g_app = &app;

    if (!initGLFW(app)) return 1;
    initImGui(app.window);

    // Inicializar renderizador
    if (!app.renderer.init(g_width, g_height)) {
        std::fprintf(stderr, "[main] Renderer init failed.\n");
        return 1;
    }

    VideoExporter video_exporter;
    if (!video_exporter.start(g_video_export, g_width, g_height)) {
        app.renderer.shutdown();
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 1;
    }

    if (g_telemetry_config.enabled) {
        app.telemetry.start(g_project_root_path,
                            g_telemetry_config.output_path,
                            RegimeConfig::BUILD_QUALITY_NAME,
                            simrng::globalSeed(),
                            g_width,
                            g_height,
                            g_video_export.enabled,
                            g_video_export.panoramic_camera,
                            reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                            reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
                            app.renderer.collectDiagnostics());
    }

    // Inicializar simulação
    app.clock.initializeToDefaultState();
    app.mgr.jumpToRegime(app.clock.getCurrentRegimeIndex(), app.clock, app.universe);
    app.universe.active_particles = static_cast<int>(app.universe.particles.activeCount());

    // Enquadrar a cena inicial a partir do conteúdo real do regime.
    recenterCameraToScene(app, app.mgr.getCurrentRegimeIndex());
    if (app.telemetry.enabled()) {
        app.telemetry.noteCheckpoint("startup", app.universe, app.renderer.collectDiagnostics());
    }

    std::printf("[main] Build quality=%s | structure particles=%d | plasma grid=%d^3 | Barnes-Hut theta=%.2f\n",
                RegimeConfig::BUILD_QUALITY_NAME,
                app.universe.quality.N_particles,
                app.universe.quality.grid_res,
                app.universe.quality.barnes_hut_theta);

    std::printf("[main] Starting simulation. Seed=%u. Keys: SPACE=play/pause, 1-9=jump, T=toggle track, C=recenter camera, R=reload shaders, H=HUD, F=fullscreen, ESC=release/quit, Ctrl+Q=quit\n",
                simrng::globalSeed());
    if (g_video_export.enabled) {
        std::printf("[main] Video mode enabled. Simulation advances deterministically at %d capture fps and exports with ffmpeg to %d fps.\n",
                    g_video_export.capture_fps, g_video_export.output_fps);
        if (g_video_export.panoramic_camera) {
            std::printf("[main] Panoramic autonomous camera enabled for video capture. preset=%s speed=%.2f distance=%.2f zoom=%.2f height=%.2f sway=%.2f smooth=%.2f\n",
                        g_video_export.panoramic_preset.c_str(),
                        g_video_export.panoramic_settings.orbit_speed,
                        g_video_export.panoramic_settings.distance_multiplier,
                        g_video_export.panoramic_settings.zoom_multiplier,
                        g_video_export.panoramic_settings.target_lift_scale,
                        g_video_export.panoramic_settings.lateral_sway_scale,
                        g_video_export.panoramic_settings.smoothing);
        }
    }

    // ── Loop principal ──────────────────────────────────────────────────────────────
    using Clock = std::chrono::steady_clock;
    auto last_time = Clock::now();
    float fps_acc  = 0.0f;
    int   fps_frames = 0;
    double sim_accumulator = 0.0;
    double panoramic_camera_time = 0.0;
    constexpr double kFixedSimDt = 1.0 / 60.0;
    constexpr int kMaxSimStepsPerFrame = 3;

    while (!glfwWindowShouldClose(app.window) && app.running) {
        const auto frame_start = Clock::now();
        TelemetryFrameTimings telemetry_timings;
        float real_dt = 0.0f;
        if (g_video_export.enabled) {
            real_dt = 1.0f / static_cast<float>(g_video_export.capture_fps);
        } else {
            auto now = Clock::now();
            real_dt = std::chrono::duration<float>(now - last_time).count();
            last_time = now;
            real_dt = std::min(real_dt, 0.1f);  // limitar para evitar espiral da morte
        }

        // Contador de FPS
        fps_acc += real_dt; fps_frames++;
        if (fps_acc >= 0.5f) {
            app.universe.fps = static_cast<float>(fps_frames) / fps_acc;
            fps_acc = 0.0f; fps_frames = 0;
        }

        auto section_start = Clock::now();
        glfwPollEvents();
        telemetry_timings.input_ms += elapsedMs(section_start, Clock::now());

        if (app.deferred_jump_regime >= 0) {
            section_start = Clock::now();
            jumpToRegimeAndFrame(app, app.deferred_jump_regime);
            app.deferred_jump_regime = -1;
            sim_accumulator = 0.0;
            telemetry_timings.deferred_jump_ms += elapsedMs(section_start, Clock::now());
        }

        // Recarregar shaders sob demanda
        if (g_reload_shaders) {
            section_start = Clock::now();
            app.renderer.reloadShaders();
            g_reload_shaders = false;
            telemetry_timings.input_ms += elapsedMs(section_start, Clock::now());
            if (app.telemetry.enabled()) {
                app.telemetry.noteShaderReload(app.renderer.collectDiagnostics());
            }
        }

        // Entrada de teclado da câmera
        section_start = Clock::now();
        InputState in{};
        in.w = glfwGetKey(app.window, GLFW_KEY_W) == GLFW_PRESS;
        in.a = glfwGetKey(app.window, GLFW_KEY_A) == GLFW_PRESS;
        in.s = glfwGetKey(app.window, GLFW_KEY_S) == GLFW_PRESS;
        in.d = glfwGetKey(app.window, GLFW_KEY_D) == GLFW_PRESS;
        in.q = glfwGetKey(app.window, GLFW_KEY_Q) == GLFW_PRESS;
        in.e = glfwGetKey(app.window, GLFW_KEY_E) == GLFW_PRESS;
        const bool panoramic_video_camera = g_video_export.enabled && g_video_export.panoramic_camera;
        if (panoramic_video_camera) {
            app.camera.enableAutoFrame();
            if (app.camera.tracked_id != std::numeric_limits<uint32_t>::max()) {
                app.camera.releaseTracking();
            }
            panoramic_camera_time += static_cast<double>(real_dt);
        } else {
            app.camera.processKeyboard(in, real_dt);
        }

        // Atualizar câmera de rastreamento (manter câmera atrás da partícula seguida)
        if (app.camera.tracked_id != std::numeric_limits<uint32_t>::max()) {
            const ParticlePool& pp = app.universe.particles;
            uint32_t tid = app.camera.tracked_id;
            if (tid < pp.x.size() && (pp.flags[tid] & PF_ACTIVE)) {
                app.camera.updateTracking({pp.x[tid], pp.y[tid], pp.z[tid]});
            } else {
                app.camera.releaseTracking();  // partícula desapareceu
                if (app.telemetry.enabled()) {
                    app.telemetry.noteOperatorEvent("tracking_released particle_missing");
                }
            }
        }
        telemetry_timings.input_ms += elapsedMs(section_start, Clock::now());

        const int max_sim_steps_per_frame = g_video_export.enabled
            ? std::max(kMaxSimStepsPerFrame,
                       static_cast<int>(std::ceil(static_cast<double>(real_dt) / kFixedSimDt)) + 1)
            : kMaxSimStepsPerFrame;

        if (g_video_export.enabled) {
            sim_accumulator += static_cast<double>(real_dt);
        } else {
            sim_accumulator = std::min(sim_accumulator + static_cast<double>(real_dt),
                                       kFixedSimDt * static_cast<double>(max_sim_steps_per_frame));
        }

        section_start = Clock::now();
        int sim_steps = 0;
        while (sim_accumulator >= kFixedSimDt && sim_steps < max_sim_steps_per_frame) {
            // Avançar simulação em passo fixo para evitar que FPS baixo altere a dinâmica.
            auto step_start = Clock::now();
            app.clock.step(kFixedSimDt);
            telemetry_timings.clock_step_ms += elapsedMs(step_start, Clock::now());

            // Sincronizar universo com o relógio
            app.universe.scale_factor    = app.clock.getScaleFactor();
            app.universe.temperature_keV = app.clock.getTemperatureKeV();
            app.universe.cosmic_time     = app.clock.getCosmicTime();
            app.universe.regime_index    = app.mgr.getCurrentRegimeIndex();

            // Tick de física do regime
            int previous_regime = app.mgr.getCurrentRegimeIndex();
            if (dbgVerboseFrame(app.frame_index))
                DBGF("[DBG f=%llu] sim_step=%d ENTER mgr.tick regime=%d\n",
                     (unsigned long long)app.frame_index, sim_steps, previous_regime);
            step_start = Clock::now();
            app.mgr.tick(app.clock, app.universe, kFixedSimDt);
            telemetry_timings.manager_tick_ms += elapsedMs(step_start, Clock::now());
            if (dbgVerboseFrame(app.frame_index))
                DBGF("[DBG f=%llu] sim_step=%d EXIT  mgr.tick (%.2f ms)\n",
                     (unsigned long long)app.frame_index, sim_steps,
                     elapsedMs(step_start, Clock::now()));
            int current_regime = app.mgr.getCurrentRegimeIndex();
            app.universe.regime_index = current_regime;
            app.universe.active_particles = static_cast<int>(app.universe.particles.activeCount());

            if (current_regime != previous_regime && app.telemetry.enabled()) {
                app.telemetry.noteRegimeChange(previous_regime, current_regime, app.universe);
            }

            if (current_regime != previous_regime &&
                app.camera.tracked_id == std::numeric_limits<uint32_t>::max()) {
                recenterCameraToScene(app, current_regime);
            }

            IRegime* regime = app.mgr.getCurrentRegime();
            if (regime) {
                double cosmic_dt = app.clock.getLastStepCosmicDt();
                if (dbgVerboseFrame(app.frame_index))
                    DBGF("[DBG f=%llu] sim_step=%d ENTER regime.update regime=%d cdt=%.3e\n",
                         (unsigned long long)app.frame_index, sim_steps,
                         app.mgr.getCurrentRegimeIndex(), cosmic_dt);
                step_start = Clock::now();
                regime->update(cosmic_dt,
                               app.clock.getScaleFactor(),
                               app.clock.getTemperatureKeV(),
                               app.universe);
                const double upd_ms = elapsedMs(step_start, Clock::now());
                telemetry_timings.regime_update_ms += upd_ms;
                if (dbgVerboseFrame(app.frame_index))
                    DBGF("[DBG f=%llu] sim_step=%d EXIT  regime.update (%.2f ms)\n",
                         (unsigned long long)app.frame_index, sim_steps, upd_ms);
            }

            app.universe.active_particles = static_cast<int>(app.universe.particles.activeCount());

            sim_accumulator -= kFixedSimDt;
            ++sim_steps;
        }
        telemetry_timings.sim_total_ms += elapsedMs(section_start, Clock::now());

        if (!g_video_export.enabled && sim_steps == max_sim_steps_per_frame) {
            sim_accumulator = 0.0;
        }

        // Atualizar física do regime atual

        if (app.camera.tracked_id == std::numeric_limits<uint32_t>::max() &&
            app.camera.isAutoFrameEnabled()) {
            section_start = Clock::now();
            SceneFrame scene_frame = Camera::estimateSceneFrame(app.universe);
            if (panoramic_video_camera) {
                app.camera.updatePanoramicAutoFrame(app.mgr.getCurrentRegimeIndex(),
                                                    scene_frame.center,
                                                    scene_frame.radius,
                                                    real_dt,
                                                    panoramic_camera_time,
                                                    g_video_export.panoramic_settings);
            } else {
                app.camera.updateAutoFrame(app.mgr.getCurrentRegimeIndex(),
                                           scene_frame.center,
                                           scene_frame.radius,
                                           real_dt);
            }
            telemetry_timings.auto_frame_ms += elapsedMs(section_start, Clock::now());
        }

        // ── Renderização ─────────────────────────────────────────────────────────────
        section_start = Clock::now();
        glm::mat4 view = app.camera.getViewMatrix();
        glm::mat4 proj = app.camera.getProjectionMatrix(
            static_cast<float>(g_width) / static_cast<float>(g_height));
        app.renderer.setViewProjection(view, proj, app.camera.position);
        telemetry_timings.render_setup_ms += elapsedMs(section_start, Clock::now());

        section_start = Clock::now();
        if (dbgVerboseFrame(app.frame_index))
            DBGF("[DBG f=%llu] ENTER beginFrame\n", (unsigned long long)app.frame_index);
        app.renderer.beginFrame();
        if (dbgVerboseFrame(app.frame_index))
            DBGF("[DBG f=%llu] ENTER mgr.render regime=%d\n",
                 (unsigned long long)app.frame_index, app.mgr.getCurrentRegimeIndex());
        app.mgr.render(app.renderer, app.universe);
        if (dbgVerboseFrame(app.frame_index))
            DBGF("[DBG f=%llu] ENTER endFrame\n", (unsigned long long)app.frame_index);
        app.renderer.endFrame();
        if (dbgVerboseFrame(app.frame_index))
            DBGF("[DBG f=%llu] EXIT  endFrame (total render=%.2f ms)\n",
                 (unsigned long long)app.frame_index, elapsedMs(section_start, Clock::now()));
        telemetry_timings.render_ms += elapsedMs(section_start, Clock::now());

        // Atualizar leitura do tempo de GPU
        app.universe.gpu_time_ms = app.renderer.getLastFrameGpuMs();

        // ── ImGui ──────────────────────────────────────────────────────────────
        section_start = Clock::now();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.overlay.render(app.clock, app.mgr, app.universe, app.camera);

        ImGui::Render();
        int pending_jump_regime = app.overlay.consumePendingJumpRegime();
        if (pending_jump_regime >= 0) {
            app.deferred_jump_regime = pending_jump_regime;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDrawBuffer(GL_BACK);
        glReadBuffer(GL_BACK);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        telemetry_timings.imgui_ms += elapsedMs(section_start, Clock::now());

        section_start = Clock::now();
        if (!video_exporter.captureFrame()) {
            app.running = false;
            if (app.telemetry.enabled()) {
                app.telemetry.noteOperatorEvent("video_capture_failed");
            }
        }
        telemetry_timings.video_capture_ms += elapsedMs(section_start, Clock::now());

        section_start = Clock::now();
        if (dbgVerboseFrame(app.frame_index))
            DBGF("[DBG f=%llu] ENTER swapBuffers\n", (unsigned long long)app.frame_index);
        glfwSwapBuffers(app.window);
        if (dbgVerboseFrame(app.frame_index))
            DBGF("[DBG f=%llu] EXIT  swapBuffers (%.2f ms)\n",
                 (unsigned long long)app.frame_index, elapsedMs(section_start, Clock::now()));
        telemetry_timings.swap_buffers_ms += elapsedMs(section_start, Clock::now());

        telemetry_timings.frame_ms = elapsedMs(frame_start, Clock::now());
        if (app.telemetry.enabled()) {
            app.telemetry.recordFrame(app.frame_index,
                                      sim_steps,
                                      real_dt,
                                      app.universe,
                                      app.renderer.collectDiagnostics(),
                                      telemetry_timings);
        }
        ++app.frame_index;
    }

    // ── Limpeza ────────────────────────────────────────────────────────────────────
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    const bool video_ok = video_exporter.finish();
    if (app.telemetry.enabled()) {
        app.telemetry.finish(video_ok ? 0 : 1, app.universe, app.renderer.collectDiagnostics());
    }
    app.renderer.shutdown();
    glfwDestroyWindow(app.window);
    glfwTerminate();
    return video_ok ? 0 : 1;
}
