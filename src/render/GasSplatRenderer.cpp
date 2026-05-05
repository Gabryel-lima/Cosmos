// src/render/GasSplatRenderer.cpp — Implementação do renderizador de Gaussian splat de gás.
#include "GasSplatRenderer.hpp"
#include "../core/Camera.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>

// ── Resolução de caminho para shaders ────────────────────────────────────────
std::string GasSplatRenderer::ResolvePath(const std::string& relative) {
    // Tenta o caminho relativo primeiro; depois ../src/shaders/ para build/
    std::vector<std::string> candidates = {
        relative,
        std::string("src/shaders/") + relative.substr(relative.rfind('/') + 1),
        std::string("../src/shaders/") + relative.substr(relative.rfind('/') + 1),
    };
    for (const auto& c : candidates) {
        std::ifstream f(c);
        if (f.good()) return c;
    }
    return relative; // fallback
}

GLuint GasSplatRenderer::CompileShader(GLenum type, const std::string& path) {
    std::string resolved = ResolvePath(path);
    std::ifstream f(resolved);
    if (!f.is_open()) {
        std::fprintf(stderr, "[GasSplat] Cannot open shader: %s\n", path.c_str());
        return 0;
    }
    std::ostringstream ss; ss << f.rdbuf();
    std::string src = ss.str();
    const char* csrc = src.c_str();

    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &csrc, nullptr);
    glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[GasSplat] Shader compile error (%s):\n%s\n", path.c_str(), log);
        glDeleteShader(sh); return 0;
    }
    return sh;
}

bool GasSplatRenderer::LinkProgram(GLuint prog, GLuint vs, GLuint fs) {
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[GasSplat] Program link error:\n%s\n", log);
        return false;
    }
    return true;
}

// ── Init ──────────────────────────────────────────────────────────────────────
bool GasSplatRenderer::Init(QualityTier quality) {
    config_ = CONFIGS[static_cast<int>(quality)];

    // Compilar programa
    GLuint vs = CompileShader(GL_VERTEX_SHADER,   "src/shaders/gas_splat.vert");
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, "src/shaders/gas_splat.frag");
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return false; }

    prog_ = glCreateProgram();
    if (!LinkProgram(prog_, vs, fs)) { glDeleteProgram(prog_); prog_ = 0; return false; }

    // Cache de uniforms
    uloc_view_        = glGetUniformLocation(prog_, "u_view");
    uloc_proj_        = glGetUniformLocation(prog_, "u_proj");
    uloc_sigma_px_    = glGetUniformLocation(prog_, "u_sigma_px");
    uloc_screen_size_ = glGetUniformLocation(prog_, "u_screen_size");

    // VAO + VBOs pré-alocados (Regra 0.5 — zero alloc em Render)
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_pos_);
    glGenBuffers(1, &vbo_smth_);
    glGenBuffers(1, &vbo_temp_);
    glGenBuffers(1, &vbo_ion_);

    const int max_splats = config_.max_splats;

    // Posição (location 0, vec3)
    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_);
    glBufferData(GL_ARRAY_BUFFER, max_splats * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    // Smoothing (location 1, float)
    glBindBuffer(GL_ARRAY_BUFFER, vbo_smth_);
    glBufferData(GL_ARRAY_BUFFER, max_splats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

    // Temperature (location 2, float)
    glBindBuffer(GL_ARRAY_BUFFER, vbo_temp_);
    glBufferData(GL_ARRAY_BUFFER, max_splats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

    // Ionized (location 3, int)
    glBindBuffer(GL_ARRAY_BUFFER, vbo_ion_);
    glBufferData(GL_ARRAY_BUFFER, max_splats * sizeof(int), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_INT, 0, nullptr);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Pré-alocar buffers de CPU — zero alloc em Render (Regra 0.5)
    pos_buf_.resize(max_splats * 3);
    smth_buf_.resize(max_splats);
    temp_buf_.resize(max_splats);
    ion_buf_.resize(max_splats);

    initialized_ = true;
    return true;
}

// ── OnQualityChanged ──────────────────────────────────────────────────────────
void GasSplatRenderer::OnQualityChanged(QualityTier new_quality) {
    config_ = CONFIGS[static_cast<int>(new_quality)];
    // Sem realocação de GPU — os buffers já foram alocados para o tier maior
}

// ── UploadGasParticles ────────────────────────────────────────────────────────
void GasSplatRenderer::UploadGasParticles(const ParticlePool& particles) {
    gas_count_ = 0;
    const int max_splats = config_.max_splats;
    const size_t n = particles.size();
    const bool has_new_fields = !particles.smoothing_length.empty();

    for (size_t i = 0; i < n && gas_count_ < max_splats; ++i) {
        if (!(particles.flags[i] & PF_ACTIVE)) continue;
        if (particles.type[i] != ParticleType::GAS) continue;

        int idx3 = gas_count_ * 3;
        pos_buf_[idx3 + 0] = static_cast<float>(particles.x[i]);
        pos_buf_[idx3 + 1] = static_cast<float>(particles.y[i]);
        pos_buf_[idx3 + 2] = static_cast<float>(particles.z[i]);

        smth_buf_[gas_count_] = has_new_fields ? particles.smoothing_length[i] : 0.5f;
        temp_buf_[gas_count_] = particles.temp_particle[i];
        ion_buf_[gas_count_]  = has_new_fields ? particles.ionized[i] : 0;
        ++gas_count_;
    }

    if (gas_count_ == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, gas_count_ * 3 * sizeof(float), pos_buf_.data());

    glBindBuffer(GL_ARRAY_BUFFER, vbo_smth_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, gas_count_ * sizeof(float), smth_buf_.data());

    glBindBuffer(GL_ARRAY_BUFFER, vbo_temp_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, gas_count_ * sizeof(float), temp_buf_.data());

    glBindBuffer(GL_ARRAY_BUFFER, vbo_ion_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, gas_count_ * sizeof(int), ion_buf_.data());

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ── UpdateIonization ─────────────────────────────────────────────────────────
void GasSplatRenderer::UpdateIonization(ParticlePool& particles,
                                         float avg_stellar_luminosity,
                                         int regime) {
    if (regime < REGIME_REIONIZATION) return;
    if (particles.ionized.empty()) return;
    if (avg_stellar_luminosity <= 0.0f) return;

    // Factor para Primeiras Estrelas (R7) vs estrelas normais (R8)
    const float lum_factor = (regime == REGIME_REIONIZATION) ? 10.0f : 1.0f;
    const float base_radius = 1.2f; // unidades de sim — tier MEDIUM/padrão

    const size_t n = particles.size();
    for (size_t i = 0; i < n; ++i) {
        if (!(particles.flags[i] & PF_ACTIVE)) continue;
        if (particles.type[i] != ParticleType::STAR) continue;

        float lum = particles.luminosity[i] * lum_factor;
        float rs  = base_radius * std::sqrt(lum / avg_stellar_luminosity);

        double sx = particles.x[i], sy = particles.y[i], sz = particles.z[i];

        for (size_t j = 0; j < n; ++j) {
            if (!(particles.flags[j] & PF_ACTIVE)) continue;
            if (particles.type[j] != ParticleType::GAS) continue;

            double dx = particles.x[j] - sx;
            double dy = particles.y[j] - sy;
            double dz = particles.z[j] - sz;
            float  d  = static_cast<float>(std::sqrt(dx*dx + dy*dy + dz*dz));

            if (d < rs) {
                particles.ionized[j] = 1;
            }
        }
    }
}

// ── Render ────────────────────────────────────────────────────────────────────
void GasSplatRenderer::Render(const ParticlePool& particles,
                               int regime,
                               const Camera& cam,
                               float /*sim_time_myr*/) {
    // Regra 0.1: guarda de regime
    if (regime < REGIME_DARK_AGES) return;
    if (!initialized_ || !prog_) return;

    // Regra 0.2: salvar estado OpenGL antes de qualquer modificação
    GLStateGuard state_guard;

    // Blend aditivo para acumulação de luz volumétrica
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glEnable(GL_PROGRAM_POINT_SIZE);

    UploadGasParticles(particles);

    if (gas_count_ == 0) return; // state_guard restaura ao sair

    glUseProgram(prog_);

    // Matrizes de câmera
    glm::mat4 view = cam.getViewMatrix();
    float     aspect = (screen_h_ > 0)
                       ? static_cast<float>(screen_w_) / static_cast<float>(screen_h_)
                       : 16.0f / 9.0f;
    glm::mat4 proj = cam.getProjectionMatrix(aspect);

    if (uloc_view_ >= 0)
        glUniformMatrix4fv(uloc_view_, 1, GL_FALSE, glm::value_ptr(view));
    if (uloc_proj_ >= 0)
        glUniformMatrix4fv(uloc_proj_, 1, GL_FALSE, glm::value_ptr(proj));
    if (uloc_sigma_px_ >= 0)
        glUniform1f(uloc_sigma_px_, config_.sigma_px);
    if (uloc_screen_size_ >= 0)
        glUniform2f(uloc_screen_size_,
                    static_cast<float>(screen_w_),
                    static_cast<float>(screen_h_));

    glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, gas_count_);
    glBindVertexArray(0);

    glUseProgram(0);
    // state_guard sai de escopo → estado restaurado automaticamente
}

// ── Shutdown ──────────────────────────────────────────────────────────────────
void GasSplatRenderer::Shutdown() {
    if (vao_)      { glDeleteVertexArrays(1, &vao_);      vao_ = 0; }
    if (vbo_pos_)  { glDeleteBuffers(1, &vbo_pos_);       vbo_pos_ = 0; }
    if (vbo_smth_) { glDeleteBuffers(1, &vbo_smth_);      vbo_smth_ = 0; }
    if (vbo_temp_) { glDeleteBuffers(1, &vbo_temp_);      vbo_temp_ = 0; }
    if (vbo_ion_)  { glDeleteBuffers(1, &vbo_ion_);       vbo_ion_ = 0; }
    if (prog_)     { glDeleteProgram(prog_);               prog_ = 0; }
    initialized_ = false;
}
