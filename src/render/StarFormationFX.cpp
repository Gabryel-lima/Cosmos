// src/render/StarFormationFX.cpp
#include "StarFormationFX.hpp"
#include "../core/Camera.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>

static std::string ResolveFXPath(const std::string& p) {
    for (const auto& prefix : {std::string(""), std::string("src/shaders/"), std::string("../src/shaders/")}) {
        std::string base = p.substr(p.rfind('/') + 1);
        std::string c = prefix.empty() ? p : (prefix + base);
        std::ifstream f(c); if (f.good()) return c;
    }
    return p;
}

GLuint StarFormationFX::CompileShader(GLenum type, const std::string& path) {
    std::string res = ResolveFXPath(path);
    std::ifstream f(res);
    if (!f.is_open()) { std::fprintf(stderr, "[StarFX] Cannot open: %s\n", path.c_str()); return 0; }
    std::ostringstream ss; ss << f.rdbuf();
    std::string src = ss.str(); const char* c = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &c, nullptr); glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[StarFX] Compile error (%s):\n%s\n", path.c_str(), log);
        glDeleteShader(sh); return 0; }
    return sh;
}

bool StarFormationFX::LinkProgram(GLuint prog, GLuint vs, GLuint fs) {
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog); glDeleteShader(vs); glDeleteShader(fs);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[StarFX] Link error:\n%s\n", log); return false; }
    return true;
}

bool StarFormationFX::Init(QualityTier quality) {
    config_ = CONFIGS[static_cast<int>(quality)];
    const int max_ev = config_.max_events;

    GLuint vs = CompileShader(GL_VERTEX_SHADER,   "src/shaders/star_formation.vert");
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, "src/shaders/star_formation.frag");
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return false; }
    prog_ = glCreateProgram();
    if (!LinkProgram(prog_, vs, fs)) { glDeleteProgram(prog_); prog_ = 0; return false; }

    uloc_view_   = glGetUniformLocation(prog_, "u_view");
    uloc_proj_   = glGetUniformLocation(prog_, "u_proj");
    uloc_screen_ = glGetUniformLocation(prog_, "u_screen_size");

    glGenVertexArrays(1, &vao_); glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_pos_); glGenBuffers(1, &vbo_rad_); glGenBuffers(1, &vbo_pha_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_);
    glBufferData(GL_ARRAY_BUFFER, max_ev * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_rad_);
    glBufferData(GL_ARRAY_BUFFER, max_ev * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_pha_);
    glBufferData(GL_ARRAY_BUFFER, max_ev * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);

    pos_buf_.resize(max_ev * 3);
    rad_buf_.resize(max_ev);
    pha_buf_.resize(max_ev);
    events_.reserve(max_ev);

    initialized_ = true;
    return true;
}

void StarFormationFX::OnQualityChanged(QualityTier new_quality) {
    config_ = CONFIGS[static_cast<int>(new_quality)];
}

void StarFormationFX::AddEvent(const FormationEvent& ev) {
    if (static_cast<int>(events_.size()) >= config_.max_events) return;
    events_.push_back(ev);
}

// ── Update ─────────────────────────────────────────────────────────────────
// Chamado no loop de update, NÃO no render.
// Modifica apenas visual_offset_x/y/z — nunca position/velocity (Regra 0.3).
void StarFormationFX::Update(float delta_time, ParticlePool& particles, int regime) {
    if (regime < REGIME_REIONIZATION) return;

    const float t_col = config_.t_collapse;
    const float t_ign = config_.t_ignition;

    for (auto it = events_.begin(); it != events_.end(); ) {
        FormationEvent& ev = *it;
        ev.elapsed += delta_time;

        switch (ev.state) {
        case EventState::COLLAPSING: {
            float t = (t_col > 0.0f) ? (ev.elapsed / t_col) : 1.0f;
            t = std::min(t, 1.0f);
            // Mover visual_offset das partículas de gás para o centro do colapso
            // (puramente visual — não afeta integrador físico)
            if (!particles.visual_offset_x.empty()) {
                const size_t n = particles.size();
                for (size_t i = 0; i < n; ++i) {
                    if (!(particles.flags[i] & PF_ACTIVE)) continue;
                    if (particles.type[i] != ParticleType::GAS) continue;

                    float dx = static_cast<float>(particles.x[i]) - ev.center.x;
                    float dy = static_cast<float>(particles.y[i]) - ev.center.y;
                    float dz = static_cast<float>(particles.z[i]) - ev.center.z;
                    float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                    if (dist < ev.influence_radius && dist > 1e-6f) {
                        float scale = -t * 0.1f / dist;
                        particles.visual_offset_x[i] += dx * scale;
                        particles.visual_offset_y[i] += dy * scale;
                        particles.visual_offset_z[i] += dz * scale;
                    }
                }
            }
            if (t >= 1.0f) {
                ev.state   = EventState::PROTO_STAR;
                ev.elapsed = 0.0f;
            }
            break;
        }
        case EventState::PROTO_STAR: {
            float t = (t_ign > 0.0f) ? (ev.elapsed / t_ign) : 1.0f;
            t = std::min(t, 1.0f);
            ev.glow_radius = t * ev.final_radius;
            if (t >= 1.0f) {
                ev.state   = EventState::STAR_BORN;
                ev.elapsed = 0.0f;
            }
            break;
        }
        case EventState::STAR_BORN:
            // Manter por mais ~1s antes de remover
            if (ev.elapsed > 1.0f) {
                it = events_.erase(it);
                continue;
            }
            break;
        default:
            break;
        }
        ++it;
    }
}

// ── UploadEvents ─────────────────────────────────────────────────────────────
void StarFormationFX::UploadEvents() {
    draw_count_ = 0;
    const int max_ev = config_.max_events;

    for (const auto& ev : events_) {
        if (draw_count_ >= max_ev) break;
        if (ev.state == EventState::DETECTING) continue;

        int idx3 = draw_count_ * 3;
        pos_buf_[idx3+0] = ev.center.x;
        pos_buf_[idx3+1] = ev.center.y;
        pos_buf_[idx3+2] = ev.center.z;
        rad_buf_[draw_count_] = ev.glow_radius;
        pha_buf_[draw_count_] = static_cast<float>(ev.state) - 1.0f; // COLLAPSING=0, PROTO=1, BORN=2
        ++draw_count_;
    }

    if (draw_count_ == 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, draw_count_ * 3 * sizeof(float), pos_buf_.data());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_rad_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, draw_count_ * sizeof(float), rad_buf_.data());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_pha_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, draw_count_ * sizeof(float), pha_buf_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ── Render ────────────────────────────────────────────────────────────────────
void StarFormationFX::Render(const ParticlePool& /*particles*/,
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

    UploadEvents();
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

void StarFormationFX::Shutdown() {
    if (vao_)     { glDeleteVertexArrays(1, &vao_);   vao_ = 0; }
    if (vbo_pos_) { glDeleteBuffers(1, &vbo_pos_);    vbo_pos_ = 0; }
    if (vbo_rad_) { glDeleteBuffers(1, &vbo_rad_);    vbo_rad_ = 0; }
    if (vbo_pha_) { glDeleteBuffers(1, &vbo_pha_);    vbo_pha_ = 0; }
    if (prog_)    { glDeleteProgram(prog_);             prog_ = 0; }
    initialized_ = false;
}
