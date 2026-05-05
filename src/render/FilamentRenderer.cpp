// src/render/FilamentRenderer.cpp
#include "FilamentRenderer.hpp"
#include "../core/Camera.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <glm/gtc/type_ptr.hpp>

static std::string ResolveFLPath(const std::string& p) {
    for (const auto& prefix : {std::string(""), std::string("src/shaders/"), std::string("../src/shaders/")}) {
        std::string base = p.substr(p.rfind('/') + 1);
        std::string c = prefix.empty() ? p : (prefix + base);
        std::ifstream f(c); if (f.good()) return c;
    }
    return p;
}

GLuint FilamentRenderer::CompileShader(GLenum type, const std::string& path) {
    std::string res = ResolveFLPath(path);
    std::ifstream f(res);
    if (!f.is_open()) { std::fprintf(stderr, "[Filament] Cannot open: %s\n", path.c_str()); return 0; }
    std::ostringstream ss; ss << f.rdbuf();
    std::string src = ss.str(); const char* c = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &c, nullptr); glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Filament] Compile error (%s):\n%s\n", path.c_str(), log);
        glDeleteShader(sh); return 0; }
    return sh;
}

bool FilamentRenderer::LinkProgram(GLuint prog, GLuint vs, GLuint fs) {
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog); glDeleteShader(vs); glDeleteShader(fs);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Filament] Link error:\n%s\n", log); return false; }
    return true;
}

bool FilamentRenderer::Init(QualityTier quality) {
    config_ = CONFIGS[static_cast<int>(quality)];

    // Em SAFE mode não há VBO — só compilar programa para não quebrar a pipeline
    GLuint vs = CompileShader(GL_VERTEX_SHADER,   "src/shaders/filament.vert");
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, "src/shaders/filament.frag");
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return false; }
    prog_ = glCreateProgram();
    if (!LinkProgram(prog_, vs, fs)) { glDeleteProgram(prog_); prog_ = 0; return false; }

    uloc_mvp_  = glGetUniformLocation(prog_, "u_mvp");
    uloc_time_ = glGetUniformLocation(prog_, "u_time");

    if (quality == QualityTier::SAFE) {
        // SAFE: sem FoF, sem VBO de filamentos
        initialized_ = true;
        return true;
    }

    const int max_verts = config_.max_edges * config_.segments_per_edge;

    glGenVertexArrays(1, &vao_); glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_pos_a_); glGenBuffers(1, &vbo_pos_b_);
    glGenBuffers(1, &vbo_mass_);  glGenBuffers(1, &vbo_t_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_a_);
    glBufferData(GL_ARRAY_BUFFER, max_verts * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_b_);
    glBufferData(GL_ARRAY_BUFFER, max_verts * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_mass_);
    glBufferData(GL_ARRAY_BUFFER, max_verts * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_t_);
    glBufferData(GL_ARRAY_BUFFER, max_verts * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);

    buf_pos_a_.resize(max_verts * 3);
    buf_pos_b_.resize(max_verts * 3);
    buf_mass_.resize(max_verts);
    buf_t_.resize(max_verts);

    halos_.reserve(config_.max_halos);
    edges_.reserve(config_.max_edges);

    initialized_ = true;
    return true;
}

void FilamentRenderer::OnQualityChanged(QualityTier new_quality) {
    config_ = CONFIGS[static_cast<int>(new_quality)];
    InvalidateCache();
}

// ── FoF simplificado O(N²) — limitado a 1×/s (Regra 0.4) ─────────────────
void FilamentRenderer::RunFoF(const ParticlePool& particles) {
    halos_.clear();
    edges_.clear();

    // Construir lista de centros de matéria escura
    const size_t n = particles.size();
    const float ll2 = linking_length * linking_length;

    // Union-Find para FoF
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);

    // Coletar índices de DM ativos
    std::vector<size_t> dm_idx;
    dm_idx.reserve(128);
    for (size_t i = 0; i < n; ++i) {
        if ((particles.flags[i] & PF_ACTIVE) &&
            particles.type[i] == ParticleType::DARK_MATTER) {
            dm_idx.push_back(i);
        }
    }

    auto find = [&](int x) -> int {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };

    // FoF linkage
    const size_t nd = dm_idx.size();
    for (size_t i = 0; i < nd; ++i) {
        for (size_t j = i + 1; j < nd; ++j) {
            size_t pi = dm_idx[i], pj = dm_idx[j];
            float dx = static_cast<float>(particles.x[pi] - particles.x[pj]);
            float dy = static_cast<float>(particles.y[pi] - particles.y[pj]);
            float dz = static_cast<float>(particles.z[pi] - particles.z[pj]);
            if (dx*dx + dy*dy + dz*dz < ll2) {
                int ri = find(static_cast<int>(pi)), rj = find(static_cast<int>(pj));
                if (ri != rj) parent[ri] = rj;
            }
        }
    }

    // Acumular halos por grupo
    struct HaloAccum { double cx=0,cy=0,cz=0,mass=0; int cnt=0; };
    std::vector<HaloAccum> accum(n);
    for (size_t idx : dm_idx) {
        int root = find(static_cast<int>(idx));
        accum[root].cx   += particles.x[idx];
        accum[root].cy   += particles.y[idx];
        accum[root].cz   += particles.z[idx];
        accum[root].mass += particles.mass[idx];
        ++accum[root].cnt;
    }

    const int min_members = 3;
    for (size_t i = 0; i < n; ++i) {
        if (accum[i].cnt >= min_members && static_cast<int>(halos_.size()) < config_.max_halos) {
            float inv = 1.0f / accum[i].cnt;
            halos_.push_back({
                glm::vec3(
                    static_cast<float>(accum[i].cx * inv),
                    static_cast<float>(accum[i].cy * inv),
                    static_cast<float>(accum[i].cz * inv)),
                static_cast<float>(accum[i].mass)
            });
        }
    }

    // Conectar halos próximos em filamentos
    const float connect_dist2 = (linking_length * 5.0f) * (linking_length * 5.0f);
    const int nh = static_cast<int>(halos_.size());
    for (int i = 0; i < nh && static_cast<int>(edges_.size()) < config_.max_edges; ++i) {
        for (int j = i + 1; j < nh && static_cast<int>(edges_.size()) < config_.max_edges; ++j) {
            glm::vec3 d = halos_[j].center - halos_[i].center;
            if (glm::dot(d, d) < connect_dist2) {
                edges_.push_back({ i, j, halos_[i].mass + halos_[j].mass });
            }
        }
    }

    vbo_dirty_ = true;
}

// ── RebuildVBO ────────────────────────────────────────────────────────────────
void FilamentRenderer::RebuildVBO() {
    vertex_count_ = 0;
    if (edges_.empty() || !vao_) return;

    const int segs = config_.segments_per_edge;
    const int max_verts = config_.max_edges * segs;

    for (const auto& e : edges_) {
        if (vertex_count_ + segs > max_verts) break;
        const glm::vec3& pa = halos_[e.a].center;
        const glm::vec3& pb = halos_[e.b].center;

        for (int s = 0; s < segs; ++s) {
            float t  = static_cast<float>(s) / static_cast<float>(segs - 1);
            int idx  = vertex_count_;
            int idx3 = idx * 3;
            buf_pos_a_[idx3+0] = pa.x; buf_pos_a_[idx3+1] = pa.y; buf_pos_a_[idx3+2] = pa.z;
            buf_pos_b_[idx3+0] = pb.x; buf_pos_b_[idx3+1] = pb.y; buf_pos_b_[idx3+2] = pb.z;
            buf_mass_[idx] = e.mass_total;
            buf_t_[idx]    = t;
            ++vertex_count_;
        }
    }

    if (vertex_count_ == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_a_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count_ * 3 * sizeof(float), buf_pos_a_.data());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_b_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count_ * 3 * sizeof(float), buf_pos_b_.data());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_mass_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count_ * sizeof(float), buf_mass_.data());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_t_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count_ * sizeof(float), buf_t_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    vbo_dirty_ = false;
}

// ── Update ────────────────────────────────────────────────────────────────────
void FilamentRenderer::Update(float delta_time, const ParticlePool& particles, int regime) {
    if (regime != REGIME_STRUCTURE) return;
    if (config_.max_halos == 0) return; // SAFE mode — sem FoF

    fof_cooldown_ -= delta_time;
    if (fof_cooldown_ > 0.0f) return;
    fof_cooldown_ = 1.0f;  // recalcular em 1s

    RunFoF(particles);
}

// ── Render ────────────────────────────────────────────────────────────────────
void FilamentRenderer::Render(const ParticlePool& /*particles*/,
                               int regime,
                               const Camera& cam,
                               float sim_time_myr) {
    // Regra 0.1: somente Regime 8
    if (regime != REGIME_STRUCTURE) return;
    if (!initialized_ || !prog_) return;
    if (config_.max_halos == 0) return; // SAFE mode desabilitado

    if (vbo_dirty_) RebuildVBO();
    if (vertex_count_ == 0) return;

    GLStateGuard guard;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glUseProgram(prog_);
    glm::mat4 view = cam.getViewMatrix();
    float aspect = screen_h_ > 0
                   ? static_cast<float>(screen_w_) / static_cast<float>(screen_h_)
                   : 16.0f / 9.0f;
    glm::mat4 proj = cam.getProjectionMatrix(aspect);
    glm::mat4 mvp  = proj * view;

    if (uloc_mvp_ >= 0)  glUniformMatrix4fv(uloc_mvp_, 1, GL_FALSE, glm::value_ptr(mvp));
    if (uloc_time_ >= 0) glUniform1f(uloc_time_, sim_time_myr);

    glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, vertex_count_); // pontos interpolados ao longo do filamento
    glBindVertexArray(0);
    glUseProgram(0);
}

void FilamentRenderer::Shutdown() {
    if (vao_)       { glDeleteVertexArrays(1, &vao_);    vao_ = 0; }
    if (vbo_pos_a_) { glDeleteBuffers(1, &vbo_pos_a_);  vbo_pos_a_ = 0; }
    if (vbo_pos_b_) { glDeleteBuffers(1, &vbo_pos_b_);  vbo_pos_b_ = 0; }
    if (vbo_mass_)  { glDeleteBuffers(1, &vbo_mass_);   vbo_mass_ = 0; }
    if (vbo_t_)     { glDeleteBuffers(1, &vbo_t_);      vbo_t_ = 0; }
    if (prog_)      { glDeleteProgram(prog_);             prog_ = 0; }
    initialized_ = false;
}
