// src/shader_preview_main.cpp — Minimal app to preview filament shaders without running simulation

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "render/FilamentRenderer.hpp"
#include "core/Camera.hpp"
#include "physics/ParticlePool.hpp"
#include "render/ICosmicRenderer.hpp"

static void print_usage() {
    std::printf("shader_preview [--width W] [--height H] [--seed N] [--complexity N] [--quality SAFE|MEDIUM|HIGH]\n");
}

int main(int argc, char** argv) {
    int width = 1280;
    int height = 720;
    int seed = 0;
    int complexity = 1;
    QualityTier quality = QualityTier::SAFE;

    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--width" && i + 1 < argc) width = std::atoi(argv[++i]);
        else if (a == "--height" && i + 1 < argc) height = std::atoi(argv[++i]);
        else if (a == "--seed" && i + 1 < argc) seed = std::atoi(argv[++i]);
        else if (a == "--complexity" && i + 1 < argc) complexity = std::atoi(argv[++i]);
        else if (a == "--quality" && i + 1 < argc) {
            std::string q(argv[++i]);
            if (q == "SAFE") quality = QualityTier::SAFE;
            else if (q == "MEDIUM") quality = QualityTier::MEDIUM;
            else if (q == "HIGH") quality = QualityTier::HIGH;
        } else {
            print_usage();
            return 1;
        }
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "[preview] glfwInit() failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(width, height, "Cosmos Shader Preview", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[preview] Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL(glfwGetProcAddress)) {
        std::fprintf(stderr, "[preview] Failed to load OpenGL via GLAD\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    std::printf("[preview] OpenGL %s on %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    // Renderer
    FilamentRenderer fr;
    if (!fr.Init(quality)) {
        std::fprintf(stderr, "[preview] FilamentRenderer::Init failed\n");
        // continue — shader errors will be printed by the renderer
    }
    fr.SetScreenSize(width, height);
    fr.SetPreviewMode(true);
    fr.GeneratePreviewData(seed, complexity);

    Camera cam;
    cam.position = { 0.0, 0.0, 300.0 };
    cam.fov_deg = 45.0f;

    // Simple loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            fr.Shutdown();
            fr.Init(quality);
            fr.GeneratePreviewData(seed, complexity);
        }
        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) { fr.OnQualityChanged(QualityTier::SAFE); }
        if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) { fr.OnQualityChanged(QualityTier::MEDIUM); }
        if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) { fr.OnQualityChanged(QualityTier::HIGH); }

        // ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Shader Preview Controls");
        ImGui::Text("Keys: R=reload, 1/2/3=quality");
        ImGui::SliderInt("Seed", &seed, 0, 10000);
        ImGui::SliderInt("Complexity", &complexity, 1, 6);
        if (ImGui::Button("Regenerate")) fr.GeneratePreviewData(seed, complexity);
        if (ImGui::Button("Reload Shaders")) { fr.Shutdown(); fr.Init(quality); fr.GeneratePreviewData(seed, complexity); }
        ImGui::End();

        ImGui::Render();

        int fbw, fbh; glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ParticlePool empty_pp; // not used by preview renderer
        fr.Render(empty_pp, REGIME_STRUCTURE, cam, 0.0f);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    fr.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
