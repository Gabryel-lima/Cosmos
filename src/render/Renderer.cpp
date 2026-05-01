// src/render/Renderer.cpp — Implementação do renderizador OpenGL.
#include "Renderer.hpp"
#include "../core/Universe.hpp"
#include "../physics/ParticlePool.hpp"
#include "../physics/Constants.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>

namespace {

enum class ParticleShaderTag : int {
    Default = 0,
    Gluon = 1,
    Photon = 2,
    Gas = 3,
    Star = 4,
    BlackHole = 5,
    Halo = 6
};

struct RegimeVisualProfile {
    glm::vec3 particle_tint = glm::vec3(1.0f);
    float particle_halo_softness = 0.0f;
    float particle_core_boost = 1.0f;
    float particle_sparkle = 0.0f;
    float particle_streak = 0.0f;

    glm::vec3 volume_cold = glm::vec3(0.01f, 0.05f, 0.15f);
    glm::vec3 volume_warm = glm::vec3(0.6f, 0.1f, 0.4f);
    glm::vec3 volume_hot = glm::vec3(1.0f, 0.8f, 0.3f);
    glm::vec3 volume_core = glm::vec3(1.0f);
    float volume_density_scale = 10.0f;
    float volume_opacity_scale = 1.0f;
    float volume_edge_boost = 2.0f;

    float exposure = 1.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float vignette = 0.8f;
    float grain = 0.012f;
    glm::vec3 shadow_tint = glm::vec3(1.0f);
    glm::vec3 highlight_tint = glm::vec3(1.0f);

    float inflation_gradient_boost = 1.0f;
    float inflation_interference = 0.0f;
};

float mixScalar(float a, float b, float t) {
    return a + (b - a) * t;
}

glm::vec3 mixVec3(const glm::vec3& a, const glm::vec3& b, float t) {
    return a + (b - a) * t;
}

RegimeVisualProfile mixProfiles(const RegimeVisualProfile& from,
                                const RegimeVisualProfile& to,
                                float t) {
    RegimeVisualProfile out;
    out.particle_tint = mixVec3(from.particle_tint, to.particle_tint, t);
    out.particle_halo_softness = mixScalar(from.particle_halo_softness, to.particle_halo_softness, t);
    out.particle_core_boost = mixScalar(from.particle_core_boost, to.particle_core_boost, t);
    out.particle_sparkle = mixScalar(from.particle_sparkle, to.particle_sparkle, t);
    out.particle_streak = mixScalar(from.particle_streak, to.particle_streak, t);
    out.volume_cold = mixVec3(from.volume_cold, to.volume_cold, t);
    out.volume_warm = mixVec3(from.volume_warm, to.volume_warm, t);
    out.volume_hot = mixVec3(from.volume_hot, to.volume_hot, t);
    out.volume_core = mixVec3(from.volume_core, to.volume_core, t);
    out.volume_density_scale = mixScalar(from.volume_density_scale, to.volume_density_scale, t);
    out.volume_opacity_scale = mixScalar(from.volume_opacity_scale, to.volume_opacity_scale, t);
    out.volume_edge_boost = mixScalar(from.volume_edge_boost, to.volume_edge_boost, t);
    out.exposure = mixScalar(from.exposure, to.exposure, t);
    out.saturation = mixScalar(from.saturation, to.saturation, t);
    out.contrast = mixScalar(from.contrast, to.contrast, t);
    out.vignette = mixScalar(from.vignette, to.vignette, t);
    out.grain = mixScalar(from.grain, to.grain, t);
    out.shadow_tint = mixVec3(from.shadow_tint, to.shadow_tint, t);
    out.highlight_tint = mixVec3(from.highlight_tint, to.highlight_tint, t);
    out.inflation_gradient_boost = mixScalar(from.inflation_gradient_boost, to.inflation_gradient_boost, t);
    out.inflation_interference = mixScalar(from.inflation_interference, to.inflation_interference, t);
    return out;
}

ParticleShaderTag particleShaderTag(ParticleType type) {
    switch (type) {
        case ParticleType::GLUON:
            return ParticleShaderTag::Gluon;
        case ParticleType::PHOTON:
            return ParticleShaderTag::Photon;
        case ParticleType::GAS:
            return ParticleShaderTag::Gas;
        case ParticleType::STAR:
            return ParticleShaderTag::Star;
        case ParticleType::BLACKHOLE:
            return ParticleShaderTag::BlackHole;
        default:
            return ParticleShaderTag::Default;
    }
}

float particleShaderTagValue(ParticleType type) {
    return static_cast<float>(static_cast<int>(particleShaderTag(type)));
}

float particleShaderTagValue(ParticleShaderTag tag) {
    return static_cast<float>(static_cast<int>(tag));
}

RegimeVisualProfile visualProfileForRegime(int regime_index) {
    RegimeVisualProfile profile;
    switch (std::clamp(regime_index, 0, 8)) {
        case 0:
            profile.exposure = 1.12f;
            profile.saturation = 1.12f;
            profile.contrast = 1.06f;
            profile.vignette = 0.55f;
            profile.grain = 0.008f;
            profile.shadow_tint = glm::vec3(0.88f, 0.94f, 1.08f);
            profile.highlight_tint = glm::vec3(1.10f, 1.05f, 0.95f);
            profile.inflation_gradient_boost = 1.8f;
            profile.inflation_interference = 0.24f;
            break;
        case 1:
            profile.particle_tint = glm::vec3(1.22f, 0.86f, 0.60f);
            profile.particle_halo_softness = -0.30f;
            profile.particle_core_boost = 1.22f;
            profile.particle_sparkle = 0.18f;
            profile.particle_streak = 0.10f;
            profile.exposure = 1.14f;
            profile.saturation = 1.14f;
            profile.contrast = 1.05f;
            profile.vignette = 0.62f;
            profile.shadow_tint = glm::vec3(1.02f, 0.94f, 0.90f);
            profile.highlight_tint = glm::vec3(1.10f, 1.02f, 0.95f);
            break;
        case 2:
            profile.particle_tint = glm::vec3(0.94f, 1.12f, 1.22f);
            profile.particle_halo_softness = -0.15f;
            profile.particle_core_boost = 1.12f;
            profile.particle_sparkle = 0.14f;
            profile.particle_streak = 0.16f;
            profile.exposure = 1.08f;
            profile.saturation = 1.08f;
            profile.contrast = 1.04f;
            profile.vignette = 0.66f;
            profile.shadow_tint = glm::vec3(0.92f, 0.98f, 1.06f);
            profile.highlight_tint = glm::vec3(1.02f, 1.06f, 1.10f);
            break;
        case 3:
            profile.particle_tint = glm::vec3(1.25f, 0.88f, 0.62f);
            profile.particle_halo_softness = -0.45f;
            profile.particle_core_boost = 1.38f;
            profile.particle_sparkle = 0.20f;
            profile.particle_streak = 0.18f;
            profile.exposure = 1.06f;
            profile.saturation = 1.20f;
            profile.contrast = 1.08f;
            profile.vignette = 0.70f;
            profile.shadow_tint = glm::vec3(1.00f, 0.94f, 0.90f);
            profile.highlight_tint = glm::vec3(1.12f, 1.02f, 0.94f);
            break;
        case 4:
            profile.particle_tint = glm::vec3(1.08f, 1.00f, 0.88f);
            profile.particle_halo_softness = -0.55f;
            profile.particle_core_boost = 1.16f;
            profile.particle_sparkle = 0.08f;
            profile.particle_streak = 0.04f;
            profile.exposure = 1.00f;
            profile.saturation = 0.94f;
            profile.contrast = 1.03f;
            profile.vignette = 0.74f;
            profile.shadow_tint = glm::vec3(0.98f, 0.97f, 1.00f);
            profile.highlight_tint = glm::vec3(1.05f, 1.02f, 0.96f);
            break;
        case 5:
            profile.particle_tint = glm::vec3(0.90f, 1.04f, 1.18f);
            profile.particle_halo_softness = 0.10f;
            profile.particle_core_boost = 1.06f;
            profile.particle_sparkle = 0.10f;
            profile.particle_streak = 0.14f;
            profile.volume_cold = glm::vec3(0.02f, 0.06f, 0.14f);
            profile.volume_warm = glm::vec3(0.12f, 0.48f, 0.92f);
            profile.volume_hot = glm::vec3(0.96f, 0.64f, 0.22f);
            profile.volume_core = glm::vec3(1.0f, 0.98f, 0.90f);
            profile.volume_density_scale = 8.5f;
            profile.volume_opacity_scale = 0.92f;
            profile.volume_edge_boost = 2.6f;
            profile.exposure = 1.04f;
            profile.saturation = 1.03f;
            profile.contrast = 1.05f;
            profile.vignette = 0.76f;
            profile.shadow_tint = glm::vec3(0.92f, 0.98f, 1.06f);
            profile.highlight_tint = glm::vec3(1.05f, 1.04f, 0.96f);
            break;
        case 6:
            profile.particle_tint = glm::vec3(0.88f, 0.78f, 0.62f);
            profile.particle_halo_softness = 0.26f;
            profile.particle_core_boost = 0.92f;
            profile.particle_sparkle = 0.02f;
            profile.particle_streak = 0.00f;
            profile.volume_cold = glm::vec3(0.012f, 0.010f, 0.009f);
            profile.volume_warm = glm::vec3(0.18f, 0.11f, 0.06f);
            profile.volume_hot = glm::vec3(0.52f, 0.32f, 0.15f);
            profile.volume_core = glm::vec3(0.90f, 0.72f, 0.48f);
            profile.volume_density_scale = 12.8f;
            profile.volume_opacity_scale = 1.14f;
            profile.volume_edge_boost = 2.2f;
            profile.exposure = 0.88f;
            profile.saturation = 0.88f;
            profile.contrast = 1.18f;
            profile.vignette = 0.86f;
            profile.grain = 0.012f;
            profile.shadow_tint = glm::vec3(0.96f, 0.90f, 0.82f);
            profile.highlight_tint = glm::vec3(1.08f, 0.98f, 0.88f);
            break;
        case 7:
            profile.particle_tint = glm::vec3(0.92f, 0.82f, 0.68f);
            profile.particle_halo_softness = 0.12f;
            profile.particle_core_boost = 1.02f;
            profile.particle_sparkle = 0.06f;
            profile.particle_streak = 0.03f;
            profile.volume_cold = glm::vec3(0.013f, 0.011f, 0.010f);
            profile.volume_warm = glm::vec3(0.22f, 0.14f, 0.08f);
            profile.volume_hot = glm::vec3(0.62f, 0.40f, 0.20f);
            profile.volume_core = glm::vec3(0.98f, 0.88f, 0.74f);
            profile.volume_density_scale = 10.0f;
            profile.volume_opacity_scale = 1.06f;
            profile.volume_edge_boost = 3.0f;
            profile.exposure = 0.92f;
            profile.saturation = 0.94f;
            profile.contrast = 1.12f;
            profile.vignette = 0.90f;
            profile.grain = 0.011f;
            profile.shadow_tint = glm::vec3(0.94f, 0.89f, 0.82f);
            profile.highlight_tint = glm::vec3(1.06f, 0.98f, 0.90f);
            break;
        case 8:
        default:
            profile.particle_tint = glm::vec3(0.98f, 0.92f, 0.82f);
            profile.particle_halo_softness = -0.02f;
            profile.particle_core_boost = 1.18f;
            profile.particle_sparkle = 0.10f;
            profile.particle_streak = 0.03f;
            profile.volume_cold = glm::vec3(0.014f, 0.012f, 0.012f);
            profile.volume_warm = glm::vec3(0.24f, 0.15f, 0.10f);
            profile.volume_hot = glm::vec3(0.70f, 0.46f, 0.22f);
            profile.volume_core = glm::vec3(1.0f, 0.92f, 0.80f);
            profile.volume_density_scale = 10.5f;
            profile.volume_opacity_scale = 0.98f;
            profile.volume_edge_boost = 2.2f;
            profile.exposure = 0.98f;
            profile.saturation = 1.00f;
            profile.contrast = 1.10f;
            profile.vignette = 0.78f;
            profile.shadow_tint = glm::vec3(0.95f, 0.92f, 0.88f);
            profile.highlight_tint = glm::vec3(1.08f, 1.00f, 0.92f);
            break;
    }
    return profile;
}

} // namespace

// ── Carregamento de shaders ────────────────────────────────────────────────────

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::string alternate = path;
        if (alternate.rfind("../", 0) == 0) {
            alternate.erase(0, 3);
        } else {
            alternate = std::string("../") + alternate;
        }
        f.open(alternate);
    }
    if (!f.is_open()) {
        std::fprintf(stderr, "[Renderer] Cannot open shader: %s\n", path.c_str());
        return "";
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLuint Renderer::compileShader(GLenum type, const std::string& path) {
    std::string src = readFile(path);
    if (src.empty()) return 0;
    #if defined(QUALITY_SAFE)
        if (path.find("volume.frag") != std::string::npos) {
            constexpr const char* safe_volume_preamble =
                "#define COSMOS_VOLUME_MAX_STEPS 128\n"
                "#define COSMOS_VOLUME_STEP_SIZE 0.0075\n"
                "#define COSMOS_VOLUME_FBM_OCTAVES 2\n"
                "#define COSMOS_VOLUME_ALPHA_COMPENSATION 1.35\n";
            const std::size_t version_end = src.find('\n');
            if (src.rfind("#version", 0) == 0 && version_end != std::string::npos) {
                src.insert(version_end + 1, safe_volume_preamble);
            }
        }
    #endif
    const char* csrc = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &csrc, nullptr);
    glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Renderer] Shader compile error (%s):\n%s\n", path.c_str(), log);
        glDeleteShader(sh); return 0;
    }
    return sh;
}

bool Renderer::loadShaderProgram(GlShader& prog,
                                  const std::string& vert_path,
                                  const std::string& frag_path)
{
    GLuint vs = compileShader(GL_VERTEX_SHADER,   vert_path);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag_path);
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return false; }

    if (prog.id) glDeleteProgram(prog.id);
    prog.id = glCreateProgram();
    glAttachShader(prog.id, vs); glAttachShader(prog.id, fs);
    glLinkProgram(prog.id);
    glDeleteShader(vs); glDeleteShader(fs);

    GLint ok; glGetProgramiv(prog.id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetProgramInfoLog(prog.id, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Renderer] Program link error:\n%s\n", log);
        glDeleteProgram(prog.id); prog.id = 0; return false;
    }
    return true;
}

bool Renderer::loadComputeShader(GlShader& prog, const std::string& comp_path) {
    GLuint cs = compileShader(GL_COMPUTE_SHADER, comp_path);
    if (!cs) return false;
    if (prog.id) glDeleteProgram(prog.id);
    prog.id = glCreateProgram();
    glAttachShader(prog.id, cs); glLinkProgram(prog.id);
    glDeleteShader(cs);
    GLint ok; glGetProgramiv(prog.id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetProgramInfoLog(prog.id, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Renderer] Compute link error:\n%s\n", log);
        glDeleteProgram(prog.id); prog.id = 0; return false;
    }
    return true;
}

// ── Inicialização ─────────────────────────────────────────────────────────────

Renderer::Renderer() = default;

void Renderer::setVec3Uniform(GLuint program, const char* name, const glm::vec3& value) const {
    glUniform3f(glGetUniformLocation(program, name), value.x, value.y, value.z);
}

void Renderer::syncVisualTuning(const Universe& universe) {
    current_regime_ = universe.regime_index;
    exposure_multiplier_ = std::clamp(universe.visual.exposure_multiplier, 0.35f, 3.0f);
    volume_opacity_multiplier_ = std::clamp(universe.visual.volume_opacity_multiplier, 0.20f, 3.0f);
    cmb_flash_strength_ = std::clamp(universe.visual.cmb_flash_strength, 0.20f, 3.0f);
    halo_visibility_ = std::clamp(universe.visual.halo_visibility, 0.0f, 3.0f);
    halo_axis_ratio_ = std::clamp(universe.visual.halo_axis_ratio, 0.65f, 2.4f);
    halos_enabled_ = universe.visual.show_halos;
    #if defined(QUALITY_SAFE)
        if (universe.regime_index >= 6) {
            volume_opacity_multiplier_ = std::min(volume_opacity_multiplier_, 0.35f);
            halo_visibility_ = 0.0f;
            halos_enabled_ = false;
        }
    #endif
}

bool Renderer::init(int width, int height) {
    width_ = width; height_ = height;

    // Alocar objetos GL agora que o contexto está ativo
    glGenBuffers(1, &particle_pos_ssbo_.id);
    glGenBuffers(1, &particle_col_ssbo_.id);
    glGenBuffers(1, &particle_vbo_.id);
    glGenBuffers(1, &quad_vbo_.id);
    glGenVertexArrays(1, &particle_vao_.id);
    glGenVertexArrays(1, &quad_vao_.id);
    glGenFramebuffers(1, &hdr_fbo_.id);
    glGenTextures(1, &hdr_color_tex_.id);
    glGenTextures(1, &hdr_depth_tex_.id);
    glGenFramebuffers(1, &bloom_fbo_[0].id);
    glGenFramebuffers(1, &bloom_fbo_[1].id);
    glGenTextures(1, &bloom_tex_[0].id);
    glGenTextures(1, &bloom_tex_[1].id);
    glGenTextures(1, &density_3d_tex_.id);
    glGenTextures(1, &ionization_3d_tex_.id);
    glGenTextures(1, &emissivity_3d_tex_.id);
    glGenTextures(1, &inflation_2d_tex_.id);

    // Carregar shaders
    reloadShaders();

    // Configurar buffers
    setupParticleBuffers();
    setupQuadBuffers();
    setupFBOs();

    // Consultas do temporizador GPU
    glGenQueries(2, timer_query_);

    // Estado GL
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // blend aditivo para partículas

    return true;
}

void Renderer::shutdown() {
    auto deleteBuffer = [](GlBuffer& buffer) {
        if (buffer.id) {
            glDeleteBuffers(1, &buffer.id);
            buffer.id = 0;
        }
    };
    auto deleteVertexArray = [](GlVAO& vao) {
        if (vao.id) {
            glDeleteVertexArrays(1, &vao.id);
            vao.id = 0;
        }
    };
    auto deleteFramebuffer = [](GlFBO& fbo) {
        if (fbo.id) {
            glDeleteFramebuffers(1, &fbo.id);
            fbo.id = 0;
        }
    };
    auto deleteTexture = [](GlTexture& texture) {
        if (texture.id) {
            glDeleteTextures(1, &texture.id);
            texture.id = 0;
        }
    };
    auto deleteProgram = [](GlShader& shader) {
        if (shader.id) {
            glDeleteProgram(shader.id);
            shader.id = 0;
        }
    };

    deleteBuffer(particle_pos_ssbo_);
    deleteBuffer(particle_col_ssbo_);
    deleteBuffer(particle_vbo_);
    deleteBuffer(quad_vbo_);
    deleteVertexArray(particle_vao_);
    deleteVertexArray(quad_vao_);

    deleteFramebuffer(hdr_fbo_);
    deleteFramebuffer(bloom_fbo_[0]);
    deleteFramebuffer(bloom_fbo_[1]);
    deleteTexture(hdr_color_tex_);
    deleteTexture(hdr_depth_tex_);
    deleteTexture(bloom_tex_[0]);
    deleteTexture(bloom_tex_[1]);
    deleteTexture(density_3d_tex_);
    deleteTexture(ionization_3d_tex_);
    deleteTexture(emissivity_3d_tex_);
    deleteTexture(inflation_2d_tex_);

    deleteProgram(particle_shader_);
    deleteProgram(volume_shader_);
    deleteProgram(inflation_shader_);
    deleteProgram(tonemap_shader_);
    deleteProgram(bloom_threshold_shader_);
    deleteProgram(bloom_blur_shader_);

    if (timer_query_[0] || timer_query_[1]) {
        glDeleteQueries(2, timer_query_);
        timer_query_[0] = 0;
        timer_query_[1] = 0;
    }
}

void Renderer::reloadShaders() {
    // Todos os caminhos de shader relativos ao diretório de trabalho (executar de build/)
    loadShaderProgram(particle_shader_,
        "../src/shaders/particle.vert", "../src/shaders/particle.frag");
    loadShaderProgram(volume_shader_,
        "../src/shaders/volume.vert",   "../src/shaders/volume.frag");
    // inflation: quad pass + dedicated field shader (sem v_color/v_size)
    loadShaderProgram(inflation_shader_,
        "../src/shaders/quad.vert", "../src/shaders/inflation.frag");
    // post-process passes: quad.vert evita o mismatch de interface v_ray_dir
    loadShaderProgram(tonemap_shader_,
        "../src/shaders/quad.vert", "../src/shaders/tonemap.frag");
    loadShaderProgram(bloom_threshold_shader_,
        "../src/shaders/quad.vert", "../src/shaders/bloom_threshold.frag");
    loadShaderProgram(bloom_blur_shader_,
        "../src/shaders/quad.vert", "../src/shaders/bloom_blur.frag");
    std::printf("[Renderer] Shaders reloaded.\n");
}

// ── Configuração dos FBOs ───────────────────────────────────────────────────────────

void Renderer::setupFBOs() {
    // FBO HDR (FP16)
    glBindFramebuffer(GL_FRAMEBUFFER, hdr_fbo_.id);

    glBindTexture(GL_TEXTURE_2D, hdr_color_tex_.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, hdr_color_tex_.id, 0);

    glBindTexture(GL_TEXTURE_2D, hdr_depth_tex_.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width_, height_, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, hdr_depth_tex_.id, 0);

    const GLenum hdr_draw_buffers[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, hdr_draw_buffers);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // FBOs de bloom (meia resolução)
    for (int i = 0; i < 2; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo_[i].id);
        glBindTexture(GL_TEXTURE_2D, bloom_tex_[i].id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_/2, height_/2, 0,
                     GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, bloom_tex_[i].id, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Textura de densidade 3D
    glBindTexture(GL_TEXTURE_3D, density_3d_tex_.id);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, 64, 64, 64, 0,
                 GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_3D, ionization_3d_tex_.id);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, 64, 64, 64, 0,
                 GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_3D, emissivity_3d_tex_.id);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, 64, 64, 64, 0,
                 GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // Textura 2D de inflação
    glBindTexture(GL_TEXTURE_2D, inflation_2d_tex_.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, 256, 256, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void Renderer::setupParticleBuffers() {
    glBindVertexArray(particle_vao_.id);
    // Quad mínimo por billboard — posições reais vêm do SSBO
    float quad[] = { -0.5f,-0.5f, 0.5f,-0.5f, -0.5f,0.5f,
                      0.5f,-0.5f, 0.5f, 0.5f, -0.5f,0.5f };
    glBindBuffer(GL_ARRAY_BUFFER, particle_vbo_.id);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
}

void Renderer::setupQuadBuffers() {
    glBindVertexArray(quad_vao_.id);
    float quad[] = { -1,-1,0,0, 1,-1,1,0, -1,1,0,1,
                      1,-1,1,0, 1,1,1,1, -1,1,0,1 };
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_.id);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float),
                          reinterpret_cast<void*>(2*sizeof(float)));
    glBindVertexArray(0);
}

void Renderer::resize(int w, int h) {
    width_ = w; height_ = h;
    setupFBOs();
    glViewport(0, 0, w, h);
}

// ── Ciclo de vida do quadro ──────────────────────────────────────────────────────

void Renderer::beginFrame() {
    cmb_flash_alpha_ = 0.0f; // Resetar flash a cada quadro
    // Iniciar temporizador GPU
    glBeginQuery(GL_TIME_ELAPSED, timer_query_[timer_idx_]);

    glBindFramebuffer(GL_FRAMEBUFFER, hdr_fbo_.id);
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.01f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::endFrame() {
    applyPostProcess();

    glEndQuery(GL_TIME_ELAPSED);
    timer_idx_ = 1 - timer_idx_;

    // Ler temporizador do quadro anterior (evitar travamento)
    if (timer_history_ready_) {
        GLint available = 0;
        glGetQueryObjectiv(timer_query_[timer_idx_], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(timer_query_[timer_idx_], GL_QUERY_RESULT, &ns);
            last_gpu_ms_ = static_cast<float>(ns) * 1e-6f;
        }
    }
    timer_history_ready_ = true;
}

void Renderer::setViewProjection(const glm::mat4& view, const glm::mat4& proj,
                                  const glm::dvec3& cam_world_pos)
{
    view_mat_     = view;
    proj_mat_     = proj;
    cam_world_pos_= cam_world_pos;
}

void Renderer::setRegimeBlend(int from_regime, int to_regime, float blend_t) {
    blend_from_ = from_regime;
    blend_to_   = to_regime;
    blend_t_    = blend_t;
}

void Renderer::setRenderOpacity(float opacity) {
    render_opacity_ = std::clamp(opacity, 0.0f, 1.0f);
}

// ── Renderização do campo de inflação ───────────────────────────────────────────────

void Renderer::renderInflationField(const Universe& universe) {
    if (universe.phi_field.empty()) return;
    if (!inflation_shader_.id) return;
    syncVisualTuning(universe);
    const RegimeVisualProfile profile = visualProfileForRegime(universe.regime_index);

    // Enviar campo 2D para textura
    glBindTexture(GL_TEXTURE_2D, inflation_2d_tex_.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    universe.phi_NX, universe.phi_NY,
                    GL_RED, GL_FLOAT, universe.phi_field.data());

    glUseProgram(inflation_shader_.id);
    glUniform1i(glGetUniformLocation(inflation_shader_.id, "u_field_tex"), 0);
    glUniform1i(glGetUniformLocation(inflation_shader_.id, "u_mode"),
                universe.inflate_3d_t > 0.01f ? 1 : 0);
    glUniform1f(glGetUniformLocation(inflation_shader_.id, "u_extrude_t"),
                universe.inflate_3d_t);
    glUniform1f(glGetUniformLocation(inflation_shader_.id, "u_opacity"), render_opacity_);
    glUniform1f(glGetUniformLocation(inflation_shader_.id, "u_gradient_boost"),
                profile.inflation_gradient_boost);
    glUniform1f(glGetUniformLocation(inflation_shader_.id, "u_interference_strength"),
                profile.inflation_interference);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inflation_2d_tex_.id);

    // Quad de tela cheia
    glBindVertexArray(quad_vao_.id);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

// ── Renderização de partículas (billboards instanciados) ───────────────────────────

void Renderer::renderParticles(const Universe& universe) {
    const ParticlePool& p = universe.particles;
    if (p.x.empty() || !particle_shader_.id) return;
    syncVisualTuning(universe);

    size_t n = p.x.size();

    // Construir posições float relativas à câmera + cores para envio à GPU
    std::vector<float> pos_data; pos_data.reserve(n * 4);
    std::vector<float> col_data; col_data.reserve(n * 4);

    // Calcular tamanho de extensão de nuvem para não dependermos de min/max que quebram com runaway particles.
    float box_size = 5.0f;
    if (universe.regime_index >= 4) box_size = 50.0f;
    else if (universe.regime_index <= 2) box_size = 1.0f;

    float extent_size = box_size * 0.005f;
    float proj_scale = std::abs(proj_mat_[1][1]) > 1e-6f ? std::abs(proj_mat_[1][1]) : 1.0f;
    float min_screen_radius_ndc = 5.0f / static_cast<float>(std::max(height_, 1));

    auto boostDimColor = [](float& r, float& g, float& b, float visibility) {
        const float fade = std::clamp(1.0f - std::clamp(visibility, 0.0f, 1.0f), 0.0f, 1.0f);
        if (fade <= 0.0f) return;
        const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const float sat_boost = 0.36f * fade;
        const float gain = 1.0f + 0.26f * fade;
        r = std::clamp((r + (r - luma) * sat_boost) * gain, 0.0f, 3.0f);
        g = std::clamp((g + (g - luma) * sat_boost) * gain, 0.0f, 3.0f);
        b = std::clamp((b + (b - luma) * sat_boost) * gain, 0.0f, 3.0f);
    };

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        float rx = static_cast<float>(p.x[i] - cam_world_pos_.x);
        float ry = static_cast<float>(p.y[i] - cam_world_pos_.y);
        float rz = static_cast<float>(p.z[i] - cam_world_pos_.z);
        float camera_dist = std::sqrt(rx * rx + ry * ry + rz * rz);
        float visual_scale = ParticlePool::defaultVisualScale(p.type[i], p.flags[i]);
        float screen_space_size = std::max(camera_dist, 1e-5f) * min_screen_radius_ndc / proj_scale;
        float particle_sz = std::max(extent_size * visual_scale, screen_space_size * visual_scale);
        if (universe.regime_index >= 6) {
            if (p.type[i] == ParticleType::GAS) {
                if (universe.regime_index == 6) particle_sz *= 0.76f;
                else if (universe.regime_index == 7) particle_sz *= 0.84f;
                else particle_sz *= 0.90f;
            } else if (p.type[i] == ParticleType::DARK_MATTER) {
                particle_sz *= 0.84f;
            } else if (p.type[i] == ParticleType::STAR) {
                particle_sz *= (universe.regime_index == 7) ? 0.92f : 1.00f;
            }
        }
        #if defined(QUALITY_SAFE)
            if (universe.regime_index >= 6) {
                if (p.type[i] == ParticleType::GAS) {
                    particle_sz *= 1.25f;
                } else if (p.type[i] == ParticleType::STAR || p.type[i] == ParticleType::BLACKHOLE) {
                    particle_sz *= 1.12f;
                }
            }
        #endif
        pos_data.push_back(rx); pos_data.push_back(ry);
        pos_data.push_back(rz); pos_data.push_back(particle_sz);
        // Use the alpha slot as a stable shader tag instead of the raw enum id,
        // so special rendering survives enum expansions.
        float color_r = p.color_r[i] * p.luminosity[i];
        float color_g = p.color_g[i] * p.luminosity[i];
        float color_b = p.color_b[i] * p.luminosity[i];
        if (universe.regime_index >= 6) {
            if (p.type[i] == ParticleType::GAS) {
                if (universe.regime_index == 6) {
                    color_r *= 0.60f;
                    color_g *= 0.50f;
                    color_b *= 0.38f;
                } else if (universe.regime_index == 7) {
                    color_r *= 0.74f;
                    color_g *= 0.66f;
                    color_b *= 0.58f;
                } else {
                    color_r *= 0.86f;
                    color_g *= 0.80f;
                    color_b *= 0.74f;
                }
            } else if (p.type[i] == ParticleType::DARK_MATTER) {
                color_r *= 0.54f;
                color_g *= 0.48f;
                color_b *= 0.58f;
            } else if (p.type[i] == ParticleType::STAR) {
                color_r *= 1.06f;
                color_g *= 0.98f;
                color_b *= (universe.regime_index == 7) ? 0.90f : 0.84f;
            }
        }
        #if defined(QUALITY_SAFE)
            if (universe.regime_index >= 6) {
                if (p.type[i] == ParticleType::GAS) {
                    color_r *= 1.35f;
                    color_g *= 1.35f;
                    color_b *= 1.45f;
                } else if (p.type[i] == ParticleType::STAR) {
                    color_r *= 1.10f;
                    color_g *= 1.08f;
                    color_b *= 1.08f;
                } else if (p.type[i] == ParticleType::BLACKHOLE) {
                    color_r *= 1.18f;
                    color_g *= 1.12f;
                    color_b *= 1.12f;
                }
            }
        #endif
        boostDimColor(color_r, color_g, color_b, std::min(p.luminosity[i], 1.0f));
        col_data.push_back(color_r);
        col_data.push_back(color_g);
        col_data.push_back(color_b);
        col_data.push_back(particleShaderTagValue(p.type[i]));
    }

    if (pos_data.empty()) return;
    size_t draw_count = pos_data.size() / 4;

    // Enviar SSBOs
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particle_pos_ssbo_.id);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(pos_data.size() * sizeof(float)),
                 pos_data.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particle_pos_ssbo_.id);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particle_col_ssbo_.id);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(col_data.size() * sizeof(float)),
                 col_data.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, particle_col_ssbo_.id);

    glUseProgram(particle_shader_.id);
    const RegimeVisualProfile profile = visualProfileForRegime(universe.regime_index);
    glUniformMatrix4fv(glGetUniformLocation(particle_shader_.id, "u_view"),
                       1, GL_FALSE, glm::value_ptr(view_mat_));
    glUniformMatrix4fv(glGetUniformLocation(particle_shader_.id, "u_proj"),
                       1, GL_FALSE, glm::value_ptr(proj_mat_));
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_opacity"), render_opacity_);
    glUniform1i(glGetUniformLocation(particle_shader_.id, "u_regime"), universe.regime_index);
    setVec3Uniform(particle_shader_.id, "u_particle_tint", profile.particle_tint);
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_halo_softness"), profile.particle_halo_softness);
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_core_boost"), profile.particle_core_boost);
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_sparkle_gain"), profile.particle_sparkle);
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_streak_gain"), profile.particle_streak);
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_halo_axis_ratio"), halo_axis_ratio_);

    glBindVertexArray(particle_vao_.id);
    glEnable(GL_BLEND);
    if (universe.regime_index >= 4) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }
    // Desativar escrita de profundidade: partículas transparentes não devem ocluir
    // umas às outras via depth buffer (apenas partículas atrás de geometria sólida seriam
    // descartadas, mas aqui não há geometria sólida).
    glDepthMask(GL_FALSE);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(draw_count));
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
}

// ── Volume field rendering (raymarched density) ───────────────────────────────

void Renderer::renderVolumeField(const Universe& universe) {
    const GridData& field = universe.density_field;
    if (field.data.empty() || !volume_shader_.id) return;
    syncVisualTuning(universe);
    #if defined(QUALITY_SAFE)
        if (universe.regime_index >= 5) return;
    #endif
    const RegimeVisualProfile profile = visualProfileForRegime(universe.regime_index);
    const GridData* ionization = (universe.ionization_field.data.size() == field.data.size())
        ? &universe.ionization_field
        : nullptr;
    const GridData* emissivity = (universe.emissivity_field.data.size() == field.data.size())
        ? &universe.emissivity_field
        : nullptr;

    // Determine the box size based on the current cosmic regime.
    // Regime 1 and 2 operate on small boxes. Regime 3 uses a 5.0 unit box. Regime 4 uses 50.0 units.
    float box_size = 5.0f;
    if (universe.regime_index >= 4) box_size = 50.0f;
    else if (universe.regime_index <= 2) box_size = 1.0f;

    // Enviar campo para textura 3D (redimensionar se necessário)
    glBindTexture(GL_TEXTURE_3D, density_3d_tex_.id);
    if (field.NX > 0) {
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F,
                     field.NX, field.NY, field.NZ, 0,
                     GL_RED, GL_FLOAT, field.data.data());
    }
    glBindTexture(GL_TEXTURE_3D, ionization_3d_tex_.id);
    if (ionization && ionization->NX > 0) {
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F,
                     ionization->NX, ionization->NY, ionization->NZ, 0,
                     GL_RED, GL_FLOAT, ionization->data.data());
    } else if (field.NX > 0) {
        std::vector<float> zeros(field.data.size(), 0.0f);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F,
                     field.NX, field.NY, field.NZ, 0,
                     GL_RED, GL_FLOAT, zeros.data());
    }
    glBindTexture(GL_TEXTURE_3D, emissivity_3d_tex_.id);
    if (emissivity && emissivity->NX > 0) {
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F,
                     emissivity->NX, emissivity->NY, emissivity->NZ, 0,
                     GL_RED, GL_FLOAT, emissivity->data.data());
    } else if (field.NX > 0) {
        std::vector<float> zeros(field.data.size(), 0.0f);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F,
                     field.NX, field.NY, field.NZ, 0,
                     GL_RED, GL_FLOAT, zeros.data());
    }

    glUseProgram(volume_shader_.id);
    glUniform1i(glGetUniformLocation(volume_shader_.id, "u_density_tex"), 0);
    glUniform1i(glGetUniformLocation(volume_shader_.id, "u_ionization_tex"), 1);
    glUniform1i(glGetUniformLocation(volume_shader_.id, "u_emissivity_tex"), 2);
    glUniform1i(glGetUniformLocation(volume_shader_.id, "u_regime"), universe.regime_index);
    glUniform1f(glGetUniformLocation(volume_shader_.id, "u_density_scale"), profile.volume_density_scale);
    glUniform1f(glGetUniformLocation(volume_shader_.id, "u_opacity_scale"), profile.volume_opacity_scale * volume_opacity_multiplier_);
    glUniform1f(glGetUniformLocation(volume_shader_.id, "u_opacity"), render_opacity_);
    glUniform1f(glGetUniformLocation(volume_shader_.id, "u_edge_boost"), profile.volume_edge_boost);
    glUniform3f(glGetUniformLocation(volume_shader_.id, "u_cam_world_pos"), 
                static_cast<float>(cam_world_pos_.x), 
                static_cast<float>(cam_world_pos_.y), 
                static_cast<float>(cam_world_pos_.z));
    glUniform1f(glGetUniformLocation(volume_shader_.id, "u_box_size"), box_size);
    setVec3Uniform(volume_shader_.id, "u_color_cold", profile.volume_cold);
    setVec3Uniform(volume_shader_.id, "u_color_warm", profile.volume_warm);
    setVec3Uniform(volume_shader_.id, "u_color_hot", profile.volume_hot);
    setVec3Uniform(volume_shader_.id, "u_color_core", profile.volume_core);

    glUniformMatrix4fv(glGetUniformLocation(volume_shader_.id, "u_inv_view_proj"),
                       1, GL_FALSE,
                       glm::value_ptr(glm::inverse(proj_mat_ * view_mat_)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, density_3d_tex_.id);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, ionization_3d_tex_.id);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, emissivity_3d_tex_.id);

    glBindVertexArray(quad_vao_.id);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, 0);
    glUseProgram(0);
}

// ── Exibição de abundâncias nucleares ──────────────────────────────────────────────────
// Tratado inteiramente pelo ImGui em RegimeOverlay. Nada a desenhar no GL aqui.
void Renderer::renderNuclearAbundances(const NuclearAbundances& /*ab*/) {}

// ── Flash do CMB ──────────────────────────────────────────────────────────────────

void Renderer::renderCMBFlash(float t) {
    // Armazena a intensidade do flash para aplicar aditivamente durante o post-process (ACES)
    // ao invés de aplicar recursivamente sobre o HDR fbo enquanto ele ainda está vinculado.
    cmb_flash_alpha_ = std::max(0.0f, 1.0f - t) * 5.0f * cmb_flash_strength_;
}

// ── Halos de galáxias (esferas em wireframe) ───────────────────────────────────────
void Renderer::renderGalaxyHalos(const HaloInfo* halos, int count) {
    if (!halos_enabled_ || halo_visibility_ <= 0.01f || halos == nullptr || count <= 0 || !particle_shader_.id) {
        return;
    }

    std::vector<float> pos_data;
    std::vector<float> col_data;
    pos_data.reserve(static_cast<size_t>(count) * 4);
    col_data.reserve(static_cast<size_t>(count) * 4);

    const float regime_mix = std::clamp((static_cast<float>(current_regime_) - 6.0f) / 2.0f, 0.0f, 1.0f);
    const glm::vec3 halo_cold = glm::mix(glm::vec3(0.22f, 0.58f, 1.00f), glm::vec3(0.46f, 0.94f, 1.00f), regime_mix * 0.35f);
    const glm::vec3 halo_hot = glm::mix(glm::vec3(1.00f, 0.78f, 0.36f), glm::vec3(1.00f, 0.54f, 0.18f), regime_mix);

    for (int i = 0; i < count; ++i) {
        float rx = static_cast<float>(halos[i].cx - cam_world_pos_.x);
        float ry = static_cast<float>(halos[i].cy - cam_world_pos_.y);
        float rz = static_cast<float>(halos[i].cz - cam_world_pos_.z);
        float member_scale = std::log1pf(static_cast<float>(std::max(halos[i].member_count, 1)));
        float mass_scale = std::log10(static_cast<float>(std::max(halos[i].mass, 1.0)) + 1.0f);
        float emissive_scale = std::clamp(static_cast<float>(halos[i].emissivity) / 8.0f, 0.0f, 1.0f);
        float halo_size = std::clamp(static_cast<float>(halos[i].radius) * 0.32f
                                   + (0.22f + member_scale * 0.22f + mass_scale * 0.04f + emissive_scale * 0.35f) * halo_visibility_,
                                     0.35f, 4.20f);

        pos_data.push_back(rx);
        pos_data.push_back(ry);
        pos_data.push_back(rz);
        pos_data.push_back(halo_size);

        const glm::vec3 halo_color = glm::mix(halo_cold, halo_hot, emissive_scale * 0.82f)
                                   * glm::mix(0.92f, 1.12f, std::clamp(halos[i].gas_fraction, 0.0f, 1.0f));
        float glow = std::clamp(0.50f + member_scale * 0.08f + emissive_scale * 0.24f, 0.45f, 1.40f);
        col_data.push_back(halo_color.r * glow);
        col_data.push_back(halo_color.g * glow);
        col_data.push_back(halo_color.b * glow);
        col_data.push_back(particleShaderTagValue(ParticleShaderTag::Halo));
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particle_pos_ssbo_.id);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(pos_data.size() * sizeof(float)),
                 pos_data.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particle_pos_ssbo_.id);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particle_col_ssbo_.id);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(col_data.size() * sizeof(float)),
                 col_data.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, particle_col_ssbo_.id);

    const RegimeVisualProfile profile = visualProfileForRegime(current_regime_);
    glUseProgram(particle_shader_.id);
    glUniformMatrix4fv(glGetUniformLocation(particle_shader_.id, "u_view"),
                       1, GL_FALSE, glm::value_ptr(view_mat_));
    glUniformMatrix4fv(glGetUniformLocation(particle_shader_.id, "u_proj"),
                       1, GL_FALSE, glm::value_ptr(proj_mat_));
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_opacity"), render_opacity_ * std::clamp(0.65f + halo_visibility_ * 0.20f, 0.25f, 1.0f));
    glUniform1i(glGetUniformLocation(particle_shader_.id, "u_regime"), current_regime_);
    const glm::vec3 aggregate_halo_tint = glm::mix(halo_cold, halo_hot, 0.45f);
    setVec3Uniform(particle_shader_.id, "u_particle_tint", glm::mix(profile.particle_tint, aggregate_halo_tint, 0.45f));
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_halo_softness"), 0.15f);
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_core_boost"), 0.85f);
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_sparkle_gain"), 0.0f);
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_streak_gain"), 0.0f);
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_halo_axis_ratio"), halo_axis_ratio_);

    glBindVertexArray(particle_vao_.id);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
}

// ── Passagem de pós-processamento ──────────────────────────────────────────────────

void Renderer::applyPostProcess() {
    // Bloom: limiar → desfoque → composição
    // Por enquanto: blit simples do framebuffer HDR para o padrão com mapeamento de tons ACES

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    if (!tonemap_shader_.id) {
        // Fallback: apenas blit
        glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        return;
    }

    glUseProgram(tonemap_shader_.id);
    const RegimeVisualProfile profile = mixProfiles(
        visualProfileForRegime(blend_from_),
        visualProfileForRegime(blend_to_),
        std::clamp(blend_t_, 0.0f, 1.0f));
    glUniform1i(glGetUniformLocation(tonemap_shader_.id, "u_hdr_tex"), 0);
    glUniform1f(glGetUniformLocation(tonemap_shader_.id, "u_exposure"), profile.exposure * exposure_multiplier_);
    glUniform1f(glGetUniformLocation(tonemap_shader_.id, "u_cmb_flash"), cmb_flash_alpha_);
    glUniform1f(glGetUniformLocation(tonemap_shader_.id, "u_blend_t"), blend_t_);
    glUniform1f(glGetUniformLocation(tonemap_shader_.id, "u_saturation"), profile.saturation);
    glUniform1f(glGetUniformLocation(tonemap_shader_.id, "u_contrast"), profile.contrast);
    glUniform1f(glGetUniformLocation(tonemap_shader_.id, "u_vignette_strength"), profile.vignette);
    glUniform1f(glGetUniformLocation(tonemap_shader_.id, "u_grain_strength"), profile.grain);
    setVec3Uniform(tonemap_shader_.id, "u_shadow_tint", profile.shadow_tint);
    setVec3Uniform(tonemap_shader_.id, "u_highlight_tint", profile.highlight_tint);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdr_color_tex_.id);

    // Desativar blend aditivo para o pass de tonemap: queremos substituir o buffer,
    // não somar ao background.
    glDisable(GL_BLEND);
    glBindVertexArray(quad_vao_.id);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_BLEND);

    glEnable(GL_DEPTH_TEST);
}
