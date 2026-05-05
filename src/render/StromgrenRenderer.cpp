// src/render/StromgrenRenderer.cpp
#include "StromgrenRenderer.hpp"
#include "../core/Camera.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>

static std::string ResolveSPath(const std::string& p) {
    for (const auto& prefix : {std::string(""), std::string("src/shaders/"), std::string("../src/shaders/")}) {
        std::string base = p.substr(p.rfind('/') + 1);
        std::string c = prefix.empty() ? p : (prefix + base);
        std::ifstream f(c); if (f.good()) return c;
    }
    return p;
}

GLuint StromgrenRenderer::CompileShader(GLenum type, const std::string& path) {
    std::string res = ResolveSPath(path);
    std::ifstream f(res);
    if (!f.is_open()) { std::fprintf(stderr, "[Stromgren] Cannot open: %s\n", path.c_str()); return 0; }
    std::ostringstream ss; ss << f.rdbuf();
    std::string src = ss.str(); const char* c = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &c, nullptr); glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Stromgren] Compile error (%s):\n%s\n", path.c_str(), log);
        glDeleteShader(sh); return 0; }
    return sh;
}

bool StromgrenRenderer::LinkProgram(GLuint prog, GLuint vs, GLuint fs) {
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog); glDeleteShader(vs); glDeleteShader(fs);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Stromgren] Link error:\n%s\n", log); return false; }
    return true;
}

bool StromgrenRenderer::Init(QualityTier quality) {
    config_ = CONFIGS[static_cast<int>(quality)];
    const int max_pts = config_.max_sources * 3; // 3 layers

    GLuint vs = CompileShader(GL_VERTEX_SHADER,   "src/shaders/stromgren.vert");
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, "src/shaders/stromgren.frag");
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return false; }
    prog_ = glCreateProgram();
    if (!LinkProgram(prog_, vs, fs)) { glDeleteProgram(prog_); prog_ = 0; return false; }

    uloc_view_   = glGetUniformLocation(prog_, "u_view");
    uloc_proj_   = glGetUniformLocation(prog_, "u_proj");
    uloc_screen_ = glGetUniformLocation(prog_, "u_screen_size");

    glGenVertexArrays(1, &vao_); glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_pos_); glGenBuffers(1, &vbo_rad_); glGenBuffers(1, &vbo_lay_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_);
    glBufferData(GL_ARRAY_BUFFER, max_pts * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_rad_);
    glBufferData(GL_ARRAY_BUFFER, max_pts * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_lay_);
    glBufferData(GL_ARRAY_BUFFER, max_pts * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);

    pos_buf_.resize(max_pts * 3);
    rad_buf_.resize(max_pts);
    lay_buf_.resize(max_pts);

    initialized_ = true;
    return true;
}

void StromgrenRenderer::OnQualityChanged(QualityTier new_quality) {
    config_ = CONFIGS[static_cast<int>(new_quality)];
}

void StromgrenRenderer::BuildLayerBuffers(const ParticlePool& particles, int regime) {
    draw_count_ = 0;
    const int max_pts = config_.max_sources * 3;
    const float lum_factor = (regime == REGIME_REIONIZATION) ? 10.0f : 1.0f;

    // Calcular luminosidade média das estrelas para normalização
    float avg_lum = 0.0f; int star_cnt = 0;
    for (size_t i = 0; i < particles.size(); ++i) {
        if (!(particles.flags[i] & PF_ACTIVE)) continue;
        if (particles.type[i] != ParticleType::STAR) continue;
        avg_lum += particles.luminosity[i]; ++star_cnt;
    }
    if (star_cnt == 0) return;
    avg_lum = std::max(avg_lum / static_cast<float>(star_cnt), 1e-6f);

    const float layers[3] = { 0.2f, 0.6f, 1.0f };

    for (size_t i = 0; i < particles.size(); ++i) {
        if (draw_count_ >= max_pts) break;
        if (!(particles.flags[i] & PF_ACTIVE)) continue;
        if (particles.type[i] != ParticleType::STAR) continue;

        float lum = particles.luminosity[i] * lum_factor;
        float rs  = config_.base_radius * std::sqrt(lum / avg_lum);

        float px = static_cast<float>(particles.x[i]);
        float py = static_cast<float>(particles.y[i]);
        float pz = static_cast<float>(particles.z[i]);

        for (int layer = 0; layer < 3 && draw_count_ < max_pts; ++layer) {
            int idx3 = draw_count_ * 3;
            pos_buf_[idx3+0] = px;
            pos_buf_[idx3+1] = py;
            pos_buf_[idx3+2] = pz;
            rad_buf_[draw_count_] = rs * layers[layer];
            lay_buf_[draw_count_] = static_cast<float>(layer);
            ++draw_count_;
        }
    }

    if (draw_count_ == 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, draw_count_ * 3 * sizeof(float), pos_buf_.data());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_rad_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, draw_count_ * sizeof(float), rad_buf_.data());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_lay_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, draw_count_ * sizeof(float), lay_buf_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void StromgrenRenderer::Render(const ParticlePool& particles,
                                int regime,
                                const Camera& cam,
                                float /*sim_time_myr*/) {
    if (regime < REGIME_REIONIZATION) return;
    if (!initialized_ || !prog_) return;

    GLStateGuard guard;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glEnable(GL_PROGRAM_POINT_SIZE);

    BuildLayerBuffers(particles, regime);
    if (draw_count_ == 0) return;

    glUseProgram(prog_);
    glm::mat4 view = cam.getViewMatrix();
    float aspect = screen_h_ > 0
                   ? static_cast<float>(screen_w_) / static_cast<float>(screen_h_)
                   : 16.0f / 9.0f;
    glm::mat4 proj = cam.getProjectionMatrix(aspect);

    if (uloc_view_ >= 0)   glUniformMatrix4fv(uloc_view_, 1, GL_FALSE, glm::value_ptr(view));
    if (uloc_proj_ >= 0)   glUniformMatrix4fv(uloc_proj_, 1, GL_FALSE, glm::value_ptr(proj));
    if (uloc_screen_ >= 0) glUniform2f(uloc_screen_,
                                        static_cast<float>(screen_w_),
                                        static_cast<float>(screen_h_));

    glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, draw_count_);
    glBindVertexArray(0);
    glUseProgram(0);
}

void StromgrenRenderer::Shutdown() {
    if (vao_)     { glDeleteVertexArrays(1, &vao_);   vao_ = 0; }
    if (vbo_pos_) { glDeleteBuffers(1, &vbo_pos_);    vbo_pos_ = 0; }
    if (vbo_rad_) { glDeleteBuffers(1, &vbo_rad_);    vbo_rad_ = 0; }
    if (vbo_lay_) { glDeleteBuffers(1, &vbo_lay_);    vbo_lay_ = 0; }
    if (prog_)    { glDeleteProgram(prog_);             prog_ = 0; }
    initialized_ = false;
}
