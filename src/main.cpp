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
#include "core/Universe.hpp"
#include "core/Camera.hpp"
#include "render/Renderer.hpp"
#include "render/RegimeOverlay.hpp"
#include "physics/Constants.hpp"
#include "physics/ParticlePool.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <algorithm>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

// ── Estado global (apenas na unidade de tradução principal) ──────────────────────────────

static int g_width  = 1280;
static int g_height = 720;
static bool g_fullscreen    = false;
static bool g_show_hud      = true;
static bool g_reload_shaders = false;
static std::string g_imgui_ini_path = "imgui.ini";

struct VideoExportConfig {
    bool enabled = false;
    int capture_fps = 30;
    int output_fps = 60;
    std::filesystem::path output_path = "cosmos_video.mp4";
};

static VideoExportConfig g_video_export;

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
    bool           running  = true;
};

static void recenterCameraToScene(AppState& app, int regime_index) {
    SceneFrame scene_frame = Camera::estimateSceneFrame(app.universe);
    app.camera.applyState(app.camera.getSceneFittedState(regime_index, scene_frame));
}

static void jumpToRegimeAndFrame(AppState& app, int regime_index) {
    app.mgr.jumpToRegime(regime_index, app.clock, app.universe);
    recenterCameraToScene(app, regime_index);
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
    } else {
        std::printf("[Camera] No trackable particles found\n");
    }
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

static AppState* g_app = nullptr;

static void on_framebuffer_resize(GLFWwindow* /*w*/, int width, int height) {
    if (width == 0 || height == 0) return;
    g_width  = width;
    g_height = height;
    if (g_app) g_app->renderer.resize(width, height);
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
            } else {
                g_app->running = false;
            }
            break;

        case GLFW_KEY_Q:
            if (mods & GLFW_MOD_CONTROL) {
                g_app->running = false;
            }
            break;

        case GLFW_KEY_SPACE:
            if (g_app->clock.isPaused()) g_app->clock.play();
            else                         g_app->clock.pause();
            break;

        case GLFW_KEY_H:
            g_show_hud = !g_show_hud;
            g_app->overlay.visible = g_show_hud;
            break;

        case GLFW_KEY_R:
            g_reload_shaders = true; break;

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
            break;
        }

        case GLFW_KEY_1: jumpToRegimeAndFrame(*g_app, 0); break;
        case GLFW_KEY_2: jumpToRegimeAndFrame(*g_app, 1); break;
        case GLFW_KEY_3: jumpToRegimeAndFrame(*g_app, 2); break;
        case GLFW_KEY_4: jumpToRegimeAndFrame(*g_app, 3); break;
        case GLFW_KEY_5: jumpToRegimeAndFrame(*g_app, 4); break;
        case GLFW_KEY_6: jumpToRegimeAndFrame(*g_app, 5); break;
        case GLFW_KEY_7: jumpToRegimeAndFrame(*g_app, 6); break;
        case GLFW_KEY_8: jumpToRegimeAndFrame(*g_app, 7); break;
        case GLFW_KEY_9: jumpToRegimeAndFrame(*g_app, 8); break;

        case GLFW_KEY_C:
            recenterCameraToScene(*g_app, g_app->mgr.getCurrentRegimeIndex());
            std::printf("[Camera] Re-centered on scene for regime %d\n", g_app->mgr.getCurrentRegimeIndex());
            break;

        case GLFW_KEY_T:
            toggleNearestTracking(*g_app);
            break;
        }

        // Ciclar modo da câmera via Tab
        if (key == GLFW_KEY_TAB && (mods & GLFW_MOD_SHIFT) == 0) {
            g_app->camera.updateMode();
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
        break;

    case ',':
    case '<':
    case '[':
    case '{': {
        double scale = g_app->clock.getTimeScale();
        g_app->clock.setTimeScale(scale * 2.0);
        break;
    }

    case ';':
    case ':':
    case ']':
    case '}': {
        double scale = g_app->clock.getTimeScale();
        g_app->clock.setTimeScale(scale * 0.5);
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
        if (arg == "--video-capture-fps" && i + 1 < argc) {
            g_video_export.enabled = true;
            g_video_export.capture_fps = std::max(1, std::atoi(argv[++i]));
        }
        if (arg == "--video-fps" && i + 1 < argc) {
            g_video_export.enabled = true;
            g_video_export.output_fps = std::max(1, std::atoi(argv[++i]));
        }
    }
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

        g_imgui_ini_path = resolveLayoutIniPath(executable_dir).string();
        std::printf("[main] Runtime directory: %s\n", executable_dir.string().c_str());
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

    // Inicializar simulação
    app.clock.initializeToDefaultState();
    app.mgr.jumpToRegime(app.clock.getCurrentRegimeIndex(), app.clock, app.universe);

    // Enquadrar a cena inicial a partir do conteúdo real do regime.
    recenterCameraToScene(app, app.clock.getCurrentRegimeIndex());

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
    }

    // ── Loop principal ──────────────────────────────────────────────────────────────
    using Clock = std::chrono::steady_clock;
    auto last_time = Clock::now();
    float fps_acc  = 0.0f;
    int   fps_frames = 0;
    double sim_accumulator = 0.0;
    constexpr double kFixedSimDt = 1.0 / 60.0;
    constexpr int kMaxSimStepsPerFrame = 3;

    while (!glfwWindowShouldClose(app.window) && app.running) {
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

        glfwPollEvents();

        // Recarregar shaders sob demanda
        if (g_reload_shaders) {
            app.renderer.reloadShaders();
            g_reload_shaders = false;
        }

        // Entrada de teclado da câmera
        InputState in{};
        in.w = glfwGetKey(app.window, GLFW_KEY_W) == GLFW_PRESS;
        in.a = glfwGetKey(app.window, GLFW_KEY_A) == GLFW_PRESS;
        in.s = glfwGetKey(app.window, GLFW_KEY_S) == GLFW_PRESS;
        in.d = glfwGetKey(app.window, GLFW_KEY_D) == GLFW_PRESS;
        in.q = glfwGetKey(app.window, GLFW_KEY_Q) == GLFW_PRESS;
        in.e = glfwGetKey(app.window, GLFW_KEY_E) == GLFW_PRESS;
        app.camera.processKeyboard(in, real_dt);

        // Atualizar câmera de rastreamento (manter câmera atrás da partícula seguida)
        if (app.camera.tracked_id != std::numeric_limits<uint32_t>::max()) {
            const ParticlePool& pp = app.universe.particles;
            uint32_t tid = app.camera.tracked_id;
            if (tid < pp.x.size() && (pp.flags[tid] & PF_ACTIVE)) {
                app.camera.updateTracking({pp.x[tid], pp.y[tid], pp.z[tid]});
            } else {
                app.camera.releaseTracking();  // partícula desapareceu
            }
        }

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

        int sim_steps = 0;
        while (sim_accumulator >= kFixedSimDt && sim_steps < max_sim_steps_per_frame) {
            // Avançar simulação em passo fixo para evitar que FPS baixo altere a dinâmica.
            app.clock.step(kFixedSimDt);

            // Sincronizar universo com o relógio
            app.universe.scale_factor    = app.clock.getScaleFactor();
            app.universe.temperature_keV = app.clock.getTemperatureKeV();
            app.universe.cosmic_time     = app.clock.getCosmicTime();
            app.universe.regime_index    = app.mgr.getCurrentRegimeIndex();

            // Tick de física do regime
            int previous_regime = app.mgr.getCurrentRegimeIndex();
            app.mgr.tick(app.clock, app.universe, kFixedSimDt);
            int current_regime = app.mgr.getCurrentRegimeIndex();
            app.universe.regime_index = current_regime;

            if (current_regime != previous_regime &&
                app.camera.tracked_id == std::numeric_limits<uint32_t>::max()) {
                recenterCameraToScene(app, current_regime);
            }

            IRegime* regime = app.mgr.getCurrentRegime();
            if (regime) {
                double cosmic_dt = app.clock.getLastStepCosmicDt();
                regime->update(cosmic_dt,
                               app.clock.getScaleFactor(),
                               app.clock.getTemperatureKeV(),
                               app.universe);
            }

            sim_accumulator -= kFixedSimDt;
            ++sim_steps;
        }

        if (!g_video_export.enabled && sim_steps == max_sim_steps_per_frame) {
            sim_accumulator = 0.0;
        }

        // Atualizar física do regime atual

        if (app.camera.tracked_id == std::numeric_limits<uint32_t>::max() &&
            app.camera.isAutoFrameEnabled()) {
            SceneFrame scene_frame = Camera::estimateSceneFrame(app.universe);
            app.camera.updateAutoFrame(app.mgr.getCurrentRegimeIndex(),
                                       scene_frame.center,
                                       scene_frame.radius,
                                       real_dt);
        }

        // ── Renderização ─────────────────────────────────────────────────────────────
        glm::mat4 view = app.camera.getViewMatrix();
        glm::mat4 proj = app.camera.getProjectionMatrix(
            static_cast<float>(g_width) / static_cast<float>(g_height));
        app.renderer.setViewProjection(view, proj, app.camera.position);

        app.renderer.beginFrame();
        app.mgr.render(app.renderer, app.universe);
        app.renderer.endFrame();

        // Atualizar leitura do tempo de GPU
        app.universe.gpu_time_ms = app.renderer.getLastFrameGpuMs();

        // ── ImGui ──────────────────────────────────────────────────────────────
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.overlay.render(app.clock, app.mgr, app.universe, app.camera);

        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (!video_exporter.captureFrame()) {
            app.running = false;
        }

        glfwSwapBuffers(app.window);
    }

    // ── Limpeza ────────────────────────────────────────────────────────────────────
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    const bool video_ok = video_exporter.finish();
    app.renderer.shutdown();
    glfwDestroyWindow(app.window);
    glfwTerminate();
    return video_ok ? 0 : 1;
}
