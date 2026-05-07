// src/physics/NBody.cpp — Solver N-corpos com octárvore Barnes-Hut.
#include <glad/gl.h>

#include <array>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <string>

#include "NBody.hpp"
#include "Constants.hpp"
#include "ThreadPool.hpp"
#include "../core/CpuFeatures.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// SECÇÃO 1 — Declarações forward das implementações especializadas
//
// nbody_step_sse2  : definida abaixo (fallback escalar/SSE2, sempre presente)
// nbody_step_avx2  : definida em NBody_avx.cpp, compilado com -mavx2 -mfma.
//                    A declaração só existe quando o CMake confirma suporte.
// ─────────────────────────────────────────────────────────────────────────────

// Instância interna do solver Barnes-Hut compartilhada pelas duas impls.
// Thread-safety: NBody::step() não é reentrant; não há problema aqui.
static NBodySolver g_solver;

namespace {

constexpr size_t kDefaultGpuMinParticles = 4096;
constexpr GLuint kGpuWorkgroupSize = 256u;

constexpr const char* kNBodyIntegrateComputeShader = R"GLSL(#version 430
layout(local_size_x = 256) in;

layout(std430, binding = 0) buffer PosXBuffer { double x[]; };
layout(std430, binding = 1) buffer PosYBuffer { double y[]; };
layout(std430, binding = 2) buffer PosZBuffer { double z[]; };
layout(std430, binding = 3) buffer VelXBuffer { double vx[]; };
layout(std430, binding = 4) buffer VelYBuffer { double vy[]; };
layout(std430, binding = 5) buffer VelZBuffer { double vz[]; };
layout(std430, binding = 6) buffer AccXBuffer { double ax[]; };
layout(std430, binding = 7) buffer AccYBuffer { double ay[]; };
layout(std430, binding = 8) buffer AccZBuffer { double az[]; };

uniform uint u_count;
uniform double u_dt;
uniform double u_half_dt;
uniform int u_phase;

void main() {
    const uint i = gl_GlobalInvocationID.x;
    if (i >= u_count) return;

    if (u_phase == 0) {
        vx[i] += ax[i] * u_half_dt;
        vy[i] += ay[i] * u_half_dt;
        vz[i] += az[i] * u_half_dt;

        x[i] += vx[i] * u_dt;
        y[i] += vy[i] * u_dt;
        z[i] += vz[i] * u_dt;
    } else {
        vx[i] += ax[i] * u_half_dt;
        vy[i] += ay[i] * u_half_dt;
        vz[i] += az[i] * u_half_dt;
    }
}
)GLSL";

enum class NBodyBackendPreference {
    Auto,
    CpuOnly,
    GpuHybrid,
};

enum class GpuBufferSlot : GLuint {
    PosX = 0,
    PosY,
    PosZ,
    VelX,
    VelY,
    VelZ,
    AccX,
    AccY,
    AccZ,
    Count,
};

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

NBodyBackendPreference configuredBackendPreference() {
    static const NBodyBackendPreference preference = [] {
        const char* env = std::getenv("COSMOS_NBODY_BACKEND");
        if (!env || !*env) return NBodyBackendPreference::Auto;

        const std::string value = lowercase(env);
        if (value == "cpu") return NBodyBackendPreference::CpuOnly;
        if (value == "gpu" || value == "hybrid") return NBodyBackendPreference::GpuHybrid;
        return NBodyBackendPreference::Auto;
    }();
    return preference;
}

size_t configuredGpuMinParticles() {
    static const size_t min_particles = [] {
        const char* env = std::getenv("COSMOS_GPU_MIN_PARTICLES");
        if (!env || !*env) return kDefaultGpuMinParticles;

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(env, &end, 10);
        if (end == env || parsed == 0ul) return kDefaultGpuMinParticles;
        return static_cast<size_t>(parsed);
    }();
    return min_particles;
}

GLuint compileComputeProgram(const char* source) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[nbody] GPU hybrid disabled: compute shader compile failed\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);

    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[nbody] GPU hybrid disabled: compute shader link failed\n%s\n", log);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

struct NBodyGpuIntegrator {
    bool ensureAvailable() {
        if (probed_) return available_;
        probed_ = true;

        const GLubyte* version = glGetString(GL_VERSION);
        if (!version) {
            std::fprintf(stderr, "[nbody] GPU hybrid disabled: no active OpenGL context\n");
            return false;
        }

        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major < 4 || (major == 4 && minor < 3)) {
            std::fprintf(stderr, "[nbody] GPU hybrid disabled: OpenGL %d.%d lacks compute shaders\n",
                        major, minor);
            return false;
        }

        program_ = compileComputeProgram(kNBodyIntegrateComputeShader);
        if (!program_) return false;

        glGenBuffers(static_cast<GLsizei>(buffers_.size()), buffers_.data());
        available_ = std::all_of(buffers_.begin(), buffers_.end(), [](GLuint id) { return id != 0; });
        if (!available_) {
            std::fprintf(stderr, "[nbody] GPU hybrid disabled: failed to allocate SSBOs\n");
            return false;
        }

        count_loc_ = glGetUniformLocation(program_, "u_count");
        dt_loc_ = glGetUniformLocation(program_, "u_dt");
        half_dt_loc_ = glGetUniformLocation(program_, "u_half_dt");
        phase_loc_ = glGetUniformLocation(program_, "u_phase");
        return true;
    }

    void disable(const char* reason) {
        if (available_) {
            std::fprintf(stderr, "[nbody] GPU hybrid disabled after runtime error: %s\n", reason);
        }
        available_ = false;
    }

    bool kickDrift(ParticlePool& pool,
                   const std::vector<double>& ax,
                   const std::vector<double>& ay,
                   const std::vector<double>& az,
                   float dt)
    {
        if (!ensureAvailable() || !ensureCapacity(pool.size())) return false;

        if (!uploadParticleState(pool)) return false;
        if (!uploadAccelerations(ax, ay, az)) return false;
        if (!dispatch(pool.size(), dt, 0)) return false;
        return downloadParticleState(pool, true);
    }

    bool finalKick(ParticlePool& pool,
                   const std::vector<double>& ax,
                   const std::vector<double>& ay,
                   const std::vector<double>& az,
                   float dt)
    {
        if (!ensureAvailable() || !ensureCapacity(pool.size())) return false;

        if (!uploadAccelerations(ax, ay, az)) return false;
        if (!dispatch(pool.size(), dt, 1)) return false;
        return downloadParticleState(pool, false);
    }

private:
    bool ensureCapacity(size_t count) {
        if (capacity_ >= count) return true;

        const GLsizeiptr bytes = static_cast<GLsizeiptr>(count * sizeof(double));
        for (GLuint buffer : buffers_) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        if (!checkGlError("buffer allocation")) return false;
        capacity_ = count;
        return true;
    }

    bool uploadParticleState(const ParticlePool& pool) {
        return uploadSpan(GpuBufferSlot::PosX, pool.x.data(), pool.size()) &&
               uploadSpan(GpuBufferSlot::PosY, pool.y.data(), pool.size()) &&
               uploadSpan(GpuBufferSlot::PosZ, pool.z.data(), pool.size()) &&
               uploadSpan(GpuBufferSlot::VelX, pool.vx.data(), pool.size()) &&
               uploadSpan(GpuBufferSlot::VelY, pool.vy.data(), pool.size()) &&
               uploadSpan(GpuBufferSlot::VelZ, pool.vz.data(), pool.size());
    }

    bool uploadAccelerations(const std::vector<double>& ax,
                             const std::vector<double>& ay,
                             const std::vector<double>& az)
    {
        return uploadSpan(GpuBufferSlot::AccX, ax.data(), ax.size()) &&
               uploadSpan(GpuBufferSlot::AccY, ay.data(), ay.size()) &&
               uploadSpan(GpuBufferSlot::AccZ, az.data(), az.size());
    }

    bool uploadSpan(GpuBufferSlot slot, const double* data, size_t count) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers_[static_cast<size_t>(slot)]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                        static_cast<GLsizeiptr>(count * sizeof(double)), data);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        return checkGlError("buffer upload");
    }

    bool dispatch(size_t count, float dt, int phase) {
        glUseProgram(program_);
        for (size_t i = 0; i < buffers_.size(); ++i) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                             static_cast<GLuint>(i),
                             buffers_[i]);
        }

        glUniform1ui(count_loc_, static_cast<GLuint>(count));
        glUniform1d(dt_loc_, static_cast<double>(dt));
        glUniform1d(half_dt_loc_, static_cast<double>(dt) * 0.5);
        glUniform1i(phase_loc_, phase);

        const GLuint groups = static_cast<GLuint>((count + kGpuWorkgroupSize - 1u) / kGpuWorkgroupSize);
        glDispatchCompute(groups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
        glUseProgram(0);
        return checkGlError("compute dispatch");
    }

    bool downloadParticleState(ParticlePool& pool, bool with_positions) {
        if (with_positions) {
            if (!downloadSpan(GpuBufferSlot::PosX, pool.x.data(), pool.size())) return false;
            if (!downloadSpan(GpuBufferSlot::PosY, pool.y.data(), pool.size())) return false;
            if (!downloadSpan(GpuBufferSlot::PosZ, pool.z.data(), pool.size())) return false;
        }

        return downloadSpan(GpuBufferSlot::VelX, pool.vx.data(), pool.size()) &&
               downloadSpan(GpuBufferSlot::VelY, pool.vy.data(), pool.size()) &&
               downloadSpan(GpuBufferSlot::VelZ, pool.vz.data(), pool.size());
    }

    bool downloadSpan(GpuBufferSlot slot, double* data, size_t count) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers_[static_cast<size_t>(slot)]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                           static_cast<GLsizeiptr>(count * sizeof(double)), data);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        return checkGlError("buffer readback");
    }

    bool checkGlError(const char* stage) {
        const GLenum err = glGetError();
        if (err == GL_NO_ERROR) return true;

        char reason[128];
        std::snprintf(reason, sizeof(reason), "%s (0x%04x)", stage, err);
        disable(reason);
        return false;
    }

    bool probed_ = false;
    bool available_ = false;
    GLuint program_ = 0;
    GLint count_loc_ = -1;
    GLint dt_loc_ = -1;
    GLint half_dt_loc_ = -1;
    GLint phase_loc_ = -1;
    size_t capacity_ = 0;
    std::array<GLuint, static_cast<size_t>(GpuBufferSlot::Count)> buffers_{};
};

NBodyGpuIntegrator& nbodyGpuIntegrator() {
    static NBodyGpuIntegrator integrator;
    return integrator;
}

size_t configuredWorkerCount() {
    static const size_t workers = [] {
        const unsigned int hw = std::max(1u, std::thread::hardware_concurrency());
        const char* env = std::getenv("COSMOS_THREADS");
        if (!env || !*env) return static_cast<size_t>(hw);

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(env, &end, 10);
        if (end == env || parsed == 0ul) {
            return static_cast<size_t>(hw);
        }
        return static_cast<size_t>(parsed);
    }();
    return workers;
}

ThreadPool& nbodyThreadPool() {
    static ThreadPool pool(configuredWorkerCount());
    return pool;
}

void logThreadUsageOnce(size_t worker_count) {
    static std::once_flag once;
    std::call_once(once, [worker_count] {
        const char* env = std::getenv("COSMOS_THREADS");
        if (env && *env) {
            std::printf("[nbody] Force computation using %zu worker threads (COSMOS_THREADS=%s)\n",
                        worker_count,
                        env);
        } else {
            std::printf("[nbody] Force computation using %zu worker threads (hardware concurrency)\n",
                        worker_count);
        }
    });
}

void logSimdPathOnce(bool runtime_has_avx2,
                     bool dense_pool,
                     size_t active_particles,
                     size_t total_particles) {
    static std::once_flag once;
    std::call_once(once, [runtime_has_avx2, dense_pool, active_particles, total_particles] {
        #if defined(COSMOS_HAS_AVX2)
            const bool compiled_with_avx_dispatch = true;
        #else
            const bool compiled_with_avx_dispatch = false;
        #endif

        const char* path = (compiled_with_avx_dispatch && runtime_has_avx2 && dense_pool)
            ? "AVX2/FMA"
            : "SSE2/base";
        const char* reason = nullptr;

        if (!compiled_with_avx_dispatch) {
            reason = "build sem objeto AVX2";
        } else if (!runtime_has_avx2) {
            reason = "CPU sem AVX2/FMA em runtime";
        } else if (!dense_pool) {
            reason = "pool com particulas inativas; caminho seguro mantido";
        } else {
            reason = "dispatcher AVX2/FMA habilitado";
        }

        std::printf("[nbody] SIMD path=%s | cpu_avx2=%s | active=%zu/%zu | %s\n",
                    path,
                    runtime_has_avx2 ? "yes" : "no",
                    active_particles,
                    total_particles,
                    reason);
    });
}

void logBackendPathOnce(const char* backend,
                        const char* reason,
                        size_t active_particles,
                        size_t total_particles,
                        size_t gpu_min_particles) {
    static std::once_flag once;
    std::call_once(once, [backend, reason, active_particles, total_particles, gpu_min_particles] {
        std::printf("[nbody] Compute backend=%s | active=%zu/%zu | gpu_min_particles=%zu | %s\n",
                    backend,
                    active_particles,
                    total_particles,
                    gpu_min_particles,
                    reason);
    });
}

} // namespace

// Assinaturas usam NBodySolver& explicitamente para evitar globals ocultos
static void nbody_step_sse2(ParticlePool& pool, float dt,
                            float theta, float softening,
                            double acceleration_cap);
static bool nbody_step_gpu_hybrid(ParticlePool& pool, float dt,
                                  float theta, float softening,
                                  double acceleration_cap);

#if defined(COSMOS_HAS_AVX2)
    void nbody_step_avx2(ParticlePool& pool, float dt,
                         float theta, float softening,
                         double acceleration_cap);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// SECÇÃO 2 — Dispatcher (NBody é a fachada pública; NBodySolver é o motor)
//
// NBody::step() é o único ponto de entrada exposto ao resto do engine.
// Escolhe o caminho de execução uma vez por chamada (O(1) após 1ª consulta).
// ─────────────────────────────────────────────────────────────────────────────

void NBody::step(ParticlePool& pool, float dt) {
    const bool runtime_has_avx2 = cpu::has_avx2();
    const bool dense_pool = pool.activeCount() == pool.size();
    const size_t gpu_min_particles = configuredGpuMinParticles();
    const NBodyBackendPreference backend_preference = configuredBackendPreference();

    if (backend_preference != NBodyBackendPreference::CpuOnly &&
        dense_pool && pool.size() >= gpu_min_particles) {
        if (nbody_step_gpu_hybrid(pool, dt, theta, softening, acceleration_cap)) {
            logBackendPathOnce("GPU-hybrid",
                               backend_preference == NBodyBackendPreference::GpuHybrid
                                   ? "forcado por COSMOS_NBODY_BACKEND=gpu"
                                   : "auto: compute shader ativo e pool denso",
                               pool.activeCount(), pool.size(), gpu_min_particles);
            return;
        }
    }

    if (!dense_pool) {
        logBackendPathOnce("CPU",
                           "fallback: pool com particulas inativas ainda nao usa GPU",
                           pool.activeCount(), pool.size(), gpu_min_particles);
    } else if (pool.size() < gpu_min_particles) {
        logBackendPathOnce("CPU",
                           "fallback: volume abaixo do limiar automatico para GPU",
                           pool.activeCount(), pool.size(), gpu_min_particles);
    } else if (backend_preference == NBodyBackendPreference::CpuOnly) {
        logBackendPathOnce("CPU",
                           "forcado por COSMOS_NBODY_BACKEND=cpu",
                           pool.activeCount(), pool.size(), gpu_min_particles);
    } else {
        logBackendPathOnce("CPU",
                           "fallback: GPU hybrid indisponivel em runtime",
                           pool.activeCount(), pool.size(), gpu_min_particles);
    }

    logSimdPathOnce(runtime_has_avx2, dense_pool, pool.activeCount(), pool.size());

    #if defined(COSMOS_HAS_AVX2)
        if (runtime_has_avx2 && dense_pool) {
            nbody_step_avx2(pool, dt, theta, softening, acceleration_cap);
            return;
        }
    #endif
    nbody_step_sse2(pool, dt, theta, softening, acceleration_cap);
    // fallback: sempre seguro em qualquer x86-64
}

// ─────────────────────────────────────────────────────────────────────────────
// SECÇÃO 3 — Octárvore Barnes-Hut (implementação)
//
// Compilado com flags base (-march=x86-64, sem AVX).
// NBody_avx.cpp pode reimplementar apenas os loops internos com intrinsics.
// ─────────────────────────────────────────────────────────────────────────────

static int octant(const OctreeNode& node, double x, double y, double z) {
    int oct = 0;
    if (x > node.cx) oct |= 1;
    if (y > node.cy) oct |= 2;
    if (z > node.cz) oct |= 4;
    return oct;
}

static void applyAccelerationCap(const ParticlePool& pool,
                                 std::vector<double>& ax,
                                 std::vector<double>& ay,
                                 std::vector<double>& az,
                                 double acceleration_cap)
{
    if (acceleration_cap <= 0.0) return;

    for (size_t i = 0; i < pool.x.size(); ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;

        const double amag = std::sqrt(ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i]);
        if (amag <= acceleration_cap || amag == 0.0) continue;

        const double scale = acceleration_cap / amag;
        ax[i] *= scale;
        ay[i] *= scale;
        az[i] *= scale;
    }
}

static OctreeNode makeChild(const OctreeNode& parent, int oct) {
    double h = parent.half * 0.5;
    OctreeNode child;
    child.cx   = parent.cx + ((oct & 1) ? h : -h);
    child.cy   = parent.cy + ((oct & 2) ? h : -h);
    child.cz   = parent.cz + ((oct & 4) ? h : -h);
    child.half = h;
    return child;
}

OctreeNode* NBodySolver::allocateNode(double cx, double cy, double cz, double half) {
    node_pool.emplace_back();
    OctreeNode* n = &node_pool.back();
    n->cx = cx; n->cy = cy; n->cz = cz; n->half = half;
    n->com_x = n->com_y = n->com_z = 0.0;
    n->total_mass = 0.0;
    n->particle_index = -1;
    for (int i = 0; i < 8; ++i) n->children[i] = nullptr;
    return n;
}

void NBodySolver::insertParticle(OctreeNode& node, int idx,
                                  const ParticlePool& pool, int depth)
{
    if (depth > 64) return;

    double m     = pool.mass[idx];
    double total = node.total_mass + m;

    node.com_x       = (node.com_x * node.total_mass + pool.x[idx] * m) / total;
    node.com_y       = (node.com_y * node.total_mass + pool.y[idx] * m) / total;
    node.com_z       = (node.com_z * node.total_mass + pool.z[idx] * m) / total;
    node.total_mass  = total;

    if (node.particle_index == -1 && node.children[0] == nullptr) {
        node.particle_index = idx;
        return;
    }

    if (node.particle_index >= 0) {
        int old_idx = node.particle_index;
        node.particle_index = -1;
        int oct_old = octant(node, pool.x[old_idx], pool.y[old_idx], pool.z[old_idx]);
        if (!node.children[oct_old]) {
            OctreeNode child = makeChild(node, oct_old);
            node.children[oct_old] = allocateNode(child.cx, child.cy, child.cz, child.half);
        }
        insertParticle(*node.children[oct_old], old_idx, pool, depth + 1);
    }

    int oct = octant(node, pool.x[idx], pool.y[idx], pool.z[idx]);
    if (!node.children[oct]) {
        OctreeNode child = makeChild(node, oct);
        node.children[oct] = allocateNode(child.cx, child.cy, child.cz, child.half);
    }
    insertParticle(*node.children[oct], idx, pool, depth + 1);
}

void NBodySolver::buildTree(const ParticlePool& pool) {
    if (pool.x.empty()) { node_pool.clear(); root_ = nullptr; return; }

    double xmin = std::numeric_limits<double>::max();
    double xmax = std::numeric_limits<double>::lowest();
    double ymin = xmin, ymax = xmax, zmin = xmin, zmax = xmax;

    for (size_t i = 0; i < pool.x.size(); ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        xmin = std::min(xmin, pool.x[i]); xmax = std::max(xmax, pool.x[i]);
        ymin = std::min(ymin, pool.y[i]); ymax = std::max(ymax, pool.y[i]);
        zmin = std::min(zmin, pool.z[i]); zmax = std::max(zmax, pool.z[i]);
    }

    double half = std::max({xmax - xmin, ymax - ymin, zmax - zmin}) * 0.5 * 1.01;
    if (half <= 0.0) half = 1.0;

    // Reset pool and allocate root in the pool to avoid heap allocations
    node_pool.clear();
    root_ = allocateNode((xmin + xmax) * 0.5,
                         (ymin + ymax) * 0.5,
                         (zmin + zmax) * 0.5,
                         half);

    for (size_t i = 0; i < pool.x.size(); ++i) {
        if (!(pool.flags[i] & PF_ACTIVE)) continue;
        if (pool.mass[i] <= 0.0) continue;
        insertParticle(*root_, static_cast<int>(i), pool, 0);
    }
}

void NBodySolver::computeForceFromNode(const OctreeNode& node, int target_idx,
                                        const ParticlePool& pool,
                                        double& ax, double& ay, double& az) const
{
    // Iterative traversal using an explicit stack to avoid recursion overhead.
    const double tx = pool.x[target_idx];
    const double ty = pool.y[target_idx];
    const double tz = pool.z[target_idx];

    std::vector<const OctreeNode*> stack;
    stack.reserve(64);
    stack.push_back(&node);

    while (!stack.empty()) {
        const OctreeNode* cur = stack.back();
        stack.pop_back();
        if (cur->total_mass == 0.0) continue;

        double dx = cur->com_x - tx;
        double dy = cur->com_y - ty;
        double dz = cur->com_z - tz;
        double r2 = dx*dx + dy*dy + dz*dz;
        if (r2 == 0.0) continue;

        double node_size = cur->half * 2.0;
        bool use_as_point =
            (cur->particle_index >= 0 && cur->particle_index != target_idx) ||
            (node_size * node_size < theta * theta * r2);

        if (use_as_point) {
            if (cur->particle_index == target_idx) continue;
            double r  = std::sqrt(r2 + softening * softening);
            double r3 = r * r * r;
            double a  = phys::G * cur->total_mass / r3;
            ax += a * dx;
            ay += a * dy;
            az += a * dz;
        } else {
            // Push children onto stack for further traversal
            for (int oct = 0; oct < 8; ++oct) {
                if (cur->children[oct]) stack.push_back(cur->children[oct]);
            }
        }
    }
}

void NBodySolver::computeForces(const ParticlePool& pool,
                                 std::vector<double>& ax,
                                 std::vector<double>& ay,
                                 std::vector<double>& az)
{
    size_t n = pool.x.size();
    ax.assign(n, 0.0);
    ay.assign(n, 0.0);
    az.assign(n, 0.0);

    std::vector<size_t> active_indices;
    active_indices.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (pool.flags[i] & PF_ACTIVE) {
            active_indices.push_back(i);
        }
    }
    if (active_indices.empty()) return;

    buildTree(pool);
    if (!root_) return;

    constexpr size_t kMinParticlesPerTask = 512;
    const size_t worker_budget = std::min(configuredWorkerCount(), active_indices.size());
    const size_t suggested_tasks = (active_indices.size() + kMinParticlesPerTask - 1) / kMinParticlesPerTask;
    const size_t task_count = std::min(worker_budget, std::max<size_t>(1, suggested_tasks));

    if (task_count <= 1) {
        for (size_t idx : active_indices) {
            computeForceFromNode(*root_, static_cast<int>(idx), pool, ax[idx], ay[idx], az[idx]);
        }
        return;
    }

    logThreadUsageOnce(task_count);

    const size_t chunk_size = (active_indices.size() + task_count - 1) / task_count;
    auto& thread_pool = nbodyThreadPool();
    std::vector<std::future<void>> futures;
    futures.reserve(task_count);

    for (size_t task = 0; task < task_count; ++task) {
        const size_t begin = task * chunk_size;
        if (begin >= active_indices.size()) break;
        const size_t end = std::min(begin + chunk_size, active_indices.size());
        futures.push_back(thread_pool.submit([this, &pool, &ax, &ay, &az, &active_indices, begin, end] {
            for (size_t pos = begin; pos < end; ++pos) {
                const size_t idx = active_indices[pos];
                computeForceFromNode(*root_, static_cast<int>(idx), pool, ax[idx], ay[idx], az[idx]);
            }
        }));
    }

    for (auto& future : futures) {
        future.get();
    }
}

static bool nbody_step_gpu_hybrid(ParticlePool& pool, float dt,
                                  float theta, float softening,
                                  double acceleration_cap) {
    const size_t n = pool.x.size();
    if (n == 0) return true;

    std::vector<double> ax, ay, az;
    g_solver.theta = theta;
    g_solver.softening = softening;
    g_solver.computeForces(pool, ax, ay, az);
    applyAccelerationCap(pool, ax, ay, az, acceleration_cap);

    auto& gpu_integrator = nbodyGpuIntegrator();
    if (!gpu_integrator.kickDrift(pool, ax, ay, az, dt)) {
        return false;
    }

    g_solver.computeForces(pool, ax, ay, az);
    applyAccelerationCap(pool, ax, ay, az, acceleration_cap);

    if (!gpu_integrator.finalKick(pool, ax, ay, az, dt)) {
        const double half_dt = dt * 0.5;
        for (size_t i = 0; i < n; ++i) {
            if (!(pool.flags[i] & PF_ACTIVE)) continue;
            pool.vx[i] += ax[i] * half_dt;
            pool.vy[i] += ay[i] * half_dt;
            pool.vz[i] += az[i] * half_dt;
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SECÇÃO 4 — nbody_step_sse2: integrador completo, caminho SSE2/fallback
//
// Esta é a DEFINIÇÃO que o linker precisava e não encontrava antes.
// Usa g_solver (NBodySolver estático) e aplica integração Leapfrog.
// ─────────────────────────────────────────────────────────────────────────────

static void nbody_step_sse2(ParticlePool& pool, float dt,
                            float theta, float softening,
                            double acceleration_cap) {
    const size_t n = pool.x.size();
    if (n == 0) return;

    // Vetores temporários de aceleração (double para precisão gravitacional)
    std::vector<double> ax, ay, az;
    g_solver.theta = theta;
    g_solver.softening = softening;
    g_solver.computeForces(pool, ax, ay, az);
    applyAccelerationCap(pool, ax, ay, az, acceleration_cap);

    // Integração Leapfrog (kick-drift-kick): estável para órbitas de longa duração
    const double half_dt = dt * 0.5;
    // Coletar índices ativos para processamento chunked
    std::vector<size_t> active_indices;
    active_indices.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (pool.flags[i] & PF_ACTIVE) active_indices.push_back(i);
    }
    if (active_indices.empty()) return;

    // Decidir quantidade de tarefas conforme disponibilidade de workers
    constexpr size_t kMinParticlesPerTask = 512;
    const size_t worker_budget = std::min(configuredWorkerCount(), active_indices.size());
    const size_t suggested_tasks = (active_indices.size() + kMinParticlesPerTask - 1) / kMinParticlesPerTask;
    const size_t task_count = std::min(worker_budget, std::max<size_t>(1, suggested_tasks));

    // Primeiro kick + drift (pode ser paralelizado por chunks)
    if (task_count <= 1) {
        for (size_t idx : active_indices) {
            pool.vx[idx] += ax[idx] * half_dt;
            pool.vy[idx] += ay[idx] * half_dt;
            pool.vz[idx] += az[idx] * half_dt;
            pool.x[idx] += pool.vx[idx] * dt;
            pool.y[idx] += pool.vy[idx] * dt;
            pool.z[idx] += pool.vz[idx] * dt;
        }
    } else {
        logThreadUsageOnce(task_count);
        const size_t chunk_size = (active_indices.size() + task_count - 1) / task_count;
        auto& thread_pool = nbodyThreadPool();
        std::vector<std::future<void>> futures;
        futures.reserve(task_count);
        for (size_t task = 0; task < task_count; ++task) {
            const size_t begin = task * chunk_size;
            if (begin >= active_indices.size()) break;
            const size_t end = std::min(begin + chunk_size, active_indices.size());
            futures.push_back(thread_pool.submit([&pool, &ax, &ay, &az, &active_indices, begin, end, half_dt, dt] {
                for (size_t pos = begin; pos < end; ++pos) {
                    const size_t idx = active_indices[pos];
                    pool.vx[idx] += ax[idx] * half_dt;
                    pool.vy[idx] += ay[idx] * half_dt;
                    pool.vz[idx] += az[idx] * half_dt;
                    pool.x[idx] += pool.vx[idx] * dt;
                    pool.y[idx] += pool.vy[idx] * dt;
                    pool.z[idx] += pool.vz[idx] * dt;
                }
            }));
        }
        for (auto& f : futures) f.get();
    }

    // Recalcular forças com posições atualizadas para 2º kick
    g_solver.computeForces(pool, ax, ay, az);
    applyAccelerationCap(pool, ax, ay, az, acceleration_cap);

    // Kick final (½ passo de velocidade)
    if (task_count <= 1) {
        for (size_t idx : active_indices) {
            pool.vx[idx] += ax[idx] * half_dt;
            pool.vy[idx] += ay[idx] * half_dt;
            pool.vz[idx] += az[idx] * half_dt;
        }
    } else {
        const size_t chunk_size = (active_indices.size() + task_count - 1) / task_count;
        auto& thread_pool = nbodyThreadPool();
        std::vector<std::future<void>> futures2;
        futures2.reserve(task_count);
        for (size_t task = 0; task < task_count; ++task) {
            const size_t begin = task * chunk_size;
            if (begin >= active_indices.size()) break;
            const size_t end = std::min(begin + chunk_size, active_indices.size());
            futures2.push_back(thread_pool.submit([&pool, &ax, &ay, &az, &active_indices, begin, end, half_dt] {
                for (size_t pos = begin; pos < end; ++pos) {
                    const size_t idx = active_indices[pos];
                    pool.vx[idx] += ax[idx] * half_dt;
                    pool.vy[idx] += ay[idx] * half_dt;
                    pool.vz[idx] += az[idx] * half_dt;
                }
            }));
        }
        for (auto& f : futures2) f.get();
    }
}
