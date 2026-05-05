// src/render/StarGlowRenderer.cpp
#include "StarGlowRenderer.hpp"
#include "../core/Camera.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>

static std::string ResolveSGPath(const std::string& relative) {
    for (const auto& prefix : {std::string(""), std::string("src/shaders/"), std::string("../src/shaders/")}) {
        std::string base = relative.substr(relative.rfind('/') + 1);
        std::string candidate = prefix.empty() ? relative : (prefix + base);
        std::ifstream f(candidate);
        if (f.good()) return candidate;
    }
    return relative;
}

GLuint StarGlowRenderer::CompileShader(GLenum type, const std::string& path) {
    std::string resolved = ResolveSGPath(path);
    std::ifstream f(resolved);
    if (!f.is_open()) { std::fprintf(stderr, "[StarGlow] Cannot open: %s\n", path.c_str()); return 0; }
    std::ostringstream ss; ss << f.rdbuf();
    std::string src = ss.str(); const char* csrc = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &csrc, nullptr); glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[StarGlow] Compile error (%s):\n%s\n", path.c_str(), log);
        glDeleteShader(sh); return 0;
    }
    return sh;
}

bool StarGlowRenderer::LinkProgram(GLuint prog, GLuint vs, GLuint fs) {
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog); glDeleteShader(vs); glDeleteShader(fs);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[StarGlow] Link error:\n%s\n", log); return false; }
    return true;
}

bool StarGlowRenderer::Init(QualityTier quality) {
    config_ = CONFIGS[static_cast<int>(quality)];

    GLuint vs = CompileShader(GL_VERTEX_SHADER,   "src/shaders/star_glow.vert");
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, "src/shaders/star_glow.frag");
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return false; }
    prog_ = glCreateProgram();
    if (!LinkProgram(prog_, vs, fs)) { glDeleteProgram(prog_); prog_ = 0; return false; }

    uloc_view_      = glGetUniformLocation(prog_, "u_view");
    uloc_proj_      = glGetUniformLocation(prog_, "u_proj");
    uloc_base_size_ = glGetUniformLocation(prog_, "u_base_size");
    uloc_screen_    = glGetUniformLocation(prog_, "u_screen_size");

    const int max_stars = config_.max_stars;
    glGenVertexArrays(1, &vao_); glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_pos_); glGenBuffers(1, &vbo_lum_);
    glGenBuffers(1, &vbo_sta_); glGenBuffers(1, &vbo_temp_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_);
    glBufferData(GL_ARRAY_BUFFER, max_stars * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_lum_);
    glBufferData(GL_ARRAY_BUFFER, max_stars * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_sta_);
    glBufferData(GL_ARRAY_BUFFER, max_stars * sizeof(int), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2); glVertexAttribIPointer(2, 1, GL_INT, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_temp_);
    glBufferData(GL_ARRAY_BUFFER, max_stars * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);

    pos_buf_.resize(max_stars * 3);
    lum_buf_.resize(max_stars);
    sta_buf_.resize(max_stars);
    temp_buf_.resize(max_stars);

    initialized_ = true;
    return true;
}

void StarGlowRenderer::OnQualityChanged(QualityTier new_quality) {
    config_ = CONFIGS[static_cast<int>(new_quality)];
}

void StarGlowRenderer::UploadStars(const ParticlePool& particles) {
    star_count_ = 0;
    const int max_stars = config_.max_stars;
    const size_t n = particles.size();

    for (size_t i = 0; i < n && star_count_ < max_stars; ++i) {
        if (!(particles.flags[i] & PF_ACTIVE)) continue;
        if (particles.type[i] != ParticleType::STAR) continue;

        int idx3 = star_count_ * 3;
        pos_buf_[idx3+0] = static_cast<float>(particles.x[i]);
        pos_buf_[idx3+1] = static_cast<float>(particles.y[i]);
        pos_buf_[idx3+2] = static_cast<float>(particles.z[i]);
        lum_buf_[star_count_] = particles.luminosity[i];
        sta_buf_[star_count_] = static_cast<int>(particles.star_state[i]);
        temp_buf_[star_count_] = particles.temp_particle[i];
        ++star_count_;
    }
    if (star_count_ == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, star_count_ * 3 * sizeof(float), pos_buf_.data());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_lum_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, star_count_ * sizeof(float), lum_buf_.data());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_sta_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, star_count_ * sizeof(int), sta_buf_.data());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_temp_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, star_count_ * sizeof(float), temp_buf_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void StarGlowRenderer::Render(const ParticlePool& particles,
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

    UploadStars(particles);
    if (star_count_ == 0) return;

    glUseProgram(prog_);
    glm::mat4 view = cam.getViewMatrix();
    float aspect = screen_h_ > 0
                   ? static_cast<float>(screen_w_) / static_cast<float>(screen_h_)
                   : 16.0f / 9.0f;
    glm::mat4 proj = cam.getProjectionMatrix(aspect);

    if (uloc_view_ >= 0)      glUniformMatrix4fv(uloc_view_, 1, GL_FALSE, glm::value_ptr(view));
    if (uloc_proj_ >= 0)      glUniformMatrix4fv(uloc_proj_, 1, GL_FALSE, glm::value_ptr(proj));
    if (uloc_base_size_ >= 0) glUniform1f(uloc_base_size_, config_.base_size_px);
    if (uloc_screen_ >= 0)    glUniform2f(uloc_screen_,
                                           static_cast<float>(screen_w_),
                                           static_cast<float>(screen_h_));

    glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, star_count_);
    glBindVertexArray(0);
    glUseProgram(0);
}

void StarGlowRenderer::Shutdown() {
    if (vao_)      { glDeleteVertexArrays(1, &vao_);   vao_ = 0; }
    if (vbo_pos_)  { glDeleteBuffers(1, &vbo_pos_);    vbo_pos_ = 0; }
    if (vbo_lum_)  { glDeleteBuffers(1, &vbo_lum_);    vbo_lum_ = 0; }
    if (vbo_sta_)  { glDeleteBuffers(1, &vbo_sta_);    vbo_sta_ = 0; }
    if (vbo_temp_) { glDeleteBuffers(1, &vbo_temp_);   vbo_temp_ = 0; }
    if (prog_)     { glDeleteProgram(prog_);             prog_ = 0; }
    initialized_ = false;
}
