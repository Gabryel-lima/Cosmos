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
#include "core/Universe.hpp"
#include "core/Camera.hpp"
#include "render/Renderer.hpp"
#include "render/RegimeOverlay.hpp"
#include "physics/Constants.hpp"
#include "physics/ParticlePool.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string>

struct SceneFrame {
    glm::dvec3 center = {0.0, 0.0, 0.0};
    double radius = 0.0;
};

static SceneFrame estimateSceneFrame(const Universe& universe) {
    const ParticlePool& pp = universe.particles;
    SceneFrame frame;
    glm::dvec3 accum(0.0);
    size_t active_count = 0;

    for (size_t i = 0; i < pp.x.size(); ++i) {
        if (!(pp.flags[i] & PF_ACTIVE)) continue;
        accum += glm::dvec3(pp.x[i], pp.y[i], pp.z[i]);
        ++active_count;
    }

    if (active_count > 0) {
        frame.center = accum / static_cast<double>(active_count);
    }

    for (size_t i = 0; i < pp.x.size(); ++i) {
        if (!(pp.flags[i] & PF_ACTIVE)) continue;
        glm::dvec3 delta(pp.x[i], pp.y[i], pp.z[i]);
        delta -= frame.center;
        double r = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        frame.radius = std::max(frame.radius, r);
    }

    if (frame.radius <= 0.0 && universe.density_field.NX > 0) {
        frame.radius = static_cast<double>(universe.density_field.NX) * 0.5;
    }

    if (frame.radius <= 0.0) {
        switch (universe.regime_index) {
            case 0: frame.radius = 1.0; break;
            case 1:
            case 2: frame.radius = 1.5; break;
            case 3: frame.radius = 8.0; break;
            case 4: frame.radius = 30.0; break;
            default: frame.radius = 5.0; break;
        }
    }

    return frame;
}

// ── Estado global (apenas na unidade de tradução principal) ──────────────────────────────

static int g_width  = 1280;
static int g_height = 720;
static bool g_fullscreen    = false;
static bool g_show_hud      = true;
static bool g_reload_shaders = false;

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
        case GLFW_KEY_Q:
            g_app->running = false; break;

        case GLFW_KEY_SPACE:
            if (g_app->clock.isPaused()) g_app->clock.play();
            else                         g_app->clock.pause();
            break;

        case GLFW_KEY_PERIOD:  // > avançar um passo
            g_app->clock.stepSingleFrame(); break;

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

        case GLFW_KEY_LEFT_BRACKET:
        case GLFW_KEY_COMMA: {
            // Desacelerar
            double s = g_app->clock.getTimeScale();
            g_app->clock.setTimeScale(s * 0.5);
            break;
        }
        case GLFW_KEY_RIGHT_BRACKET:
        case GLFW_KEY_SEMICOLON: {
            // Acelerar
            double s = g_app->clock.getTimeScale();
            g_app->clock.setTimeScale(s * 2.0);
            break;
        }

        case GLFW_KEY_1: g_app->mgr.jumpToRegime(0, g_app->clock, g_app->universe);
                         g_app->camera.applyState(g_app->camera.getRegimeDefaultState(0)); break;
        case GLFW_KEY_2: g_app->mgr.jumpToRegime(1, g_app->clock, g_app->universe);
                         g_app->camera.applyState(g_app->camera.getRegimeDefaultState(1)); break;
        case GLFW_KEY_3: g_app->mgr.jumpToRegime(2, g_app->clock, g_app->universe);
                         g_app->camera.applyState(g_app->camera.getRegimeDefaultState(2)); break;
        case GLFW_KEY_4: g_app->mgr.jumpToRegime(3, g_app->clock, g_app->universe);
                         g_app->camera.applyState(g_app->camera.getRegimeDefaultState(3)); break;
        case GLFW_KEY_5: g_app->mgr.jumpToRegime(4, g_app->clock, g_app->universe);
                         g_app->camera.applyState(g_app->camera.getRegimeDefaultState(4)); break;

        case GLFW_KEY_C:
            g_app->camera.applyState(g_app->camera.getRegimeDefaultState(g_app->mgr.getCurrentRegimeIndex()));
            break;

        case GLFW_KEY_T: {
            // Rastrear a estrela ou buraco negro mais próximo da câmera
            const ParticlePool& pp = g_app->universe.particles;
            uint32_t best_id = std::numeric_limits<uint32_t>::max();
            double best_r2 = 1e300;
            auto cam_pos = g_app->camera.position;
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
                if (r2 < best_r2) { best_r2 = r2; best_id = static_cast<uint32_t>(i); }
            }
            if (best_id != std::numeric_limits<uint32_t>::max()) {
                g_app->camera.trackParticle(best_id);
                std::printf("[Camera] Tracking particle %u\n", best_id);
            } else {
                g_app->camera.releaseTracking();
                std::printf("[Camera] No trackable particles found\n");
            }
            break;
        }
        }

        // Ciclar modo da câmera via Tab
        if (key == GLFW_KEY_TAB && (mods & GLFW_MOD_SHIFT) == 0) {
            g_app->camera.updateMode();
        }
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

    // Rotacionar apenas com botão direito do mouse pressionado
    if (glfwGetMouseButton(g_app->window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        g_app->camera.processMouseDelta(static_cast<float>(dx),
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
    }
    return true;
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
    glfwSwapInterval(1);  // V-Sync ativado

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
    glfwSetScrollCallback(app.window, on_scroll);
    glfwSetCursorPosCallback(app.window, on_cursor_pos);

    return true;
}

static void initImGui(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");
}

// ── Principal ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    parseArgs(argc, argv);

    AppState app;
    g_app = &app;

    if (!initGLFW(app)) return 1;
    initImGui(app.window);

    // Inicializar renderizador
    if (!app.renderer.init(g_width, g_height)) {
        std::fprintf(stderr, "[main] Renderer init failed.\n");
        return 1;
    }

    // Inicializar simulação
    app.clock.initializeToDefaultState();
    app.mgr.jumpToRegime(app.clock.getCurrentRegimeIndex(), app.clock, app.universe);

    // Estado padrão da câmera para o Regime 0
    app.camera.applyState(app.camera.getRegimeDefaultState(0));

    std::printf("[main] Starting simulation. Keys: SPACE=play/pause, 1-5=jump, R=reload shaders, H=HUD, F=fullscreen, ESC=quit\n");

    // ── Loop principal ──────────────────────────────────────────────────────────────
    using Clock = std::chrono::steady_clock;
    auto last_time = Clock::now();
    float fps_acc  = 0.0f;
    int   fps_frames = 0;

    while (!glfwWindowShouldClose(app.window) && app.running) {
        // Tempo delta real
        auto now = Clock::now();
        float real_dt = std::chrono::duration<float>(now - last_time).count();
        last_time = now;
        real_dt = std::min(real_dt, 0.1f);  // limitar para evitar espiral da morte

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

        // Avançar simulação
        app.clock.step(static_cast<double>(real_dt));

        // Sincronizar universo com o relógio
        app.universe.scale_factor    = app.clock.getScaleFactor();
        app.universe.temperature_keV = app.clock.getTemperatureKeV();
        app.universe.cosmic_time     = app.clock.getCosmicTime();
        app.universe.regime_index    = app.clock.getCurrentRegimeIndex();

        // Tick de física do regime
        int previous_regime = app.mgr.getCurrentRegimeIndex();
        app.mgr.tick(app.clock, app.universe, static_cast<double>(real_dt));
        int current_regime = app.mgr.getCurrentRegimeIndex();

        if (current_regime != previous_regime &&
            app.camera.tracked_id == std::numeric_limits<uint32_t>::max()) {
            app.camera.applyState(app.camera.getRegimeDefaultState(current_regime));
        }

        IRegime* regime = app.mgr.getCurrentRegime();
        if (regime) {
            double cosmic_dt = app.clock.getLastStepCosmicDt();
            regime->update(cosmic_dt,
                           app.clock.getScaleFactor(),
                           app.clock.getTemperatureKeV(),
                           app.universe);
        }

        // Atualizar física do regime atual

        if (app.camera.tracked_id == std::numeric_limits<uint32_t>::max() &&
            app.camera.isAutoFrameEnabled()) {
            SceneFrame scene_frame = estimateSceneFrame(app.universe);
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

        glfwSwapBuffers(app.window);
    }

    // ── Limpeza ────────────────────────────────────────────────────────────────────
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    app.renderer.shutdown();
    glfwDestroyWindow(app.window);
    glfwTerminate();
    return 0;
}
