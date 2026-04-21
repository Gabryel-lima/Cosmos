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
    GLint available = 0;
    glGetQueryObjectiv(timer_query_[timer_idx_], GL_QUERY_RESULT_AVAILABLE, &available);
    if (available) {
        GLuint64 ns = 0;
        glGetQueryObjectui64v(timer_query_[timer_idx_], GL_QUERY_RESULT, &ns);
        last_gpu_ms_ = static_cast<float>(ns) * 1e-6f;
    }
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

    size_t n = p.x.size();

    // Construir posições float relativas à câmera + cores para envio à GPU
    std::vector<float> pos_data; pos_data.reserve(n * 4);
    std::vector<float> col_data; col_data.reserve(n * 4);

    // Calcular extensão da nuvem de partículas para escalar o tamanho dos billboards
    // de forma relativa à cena visível (funciona em todas as épocas cósmicas).
    // Encontra a primeira partícula activa para inicializar min/max corretamente.
    size_t first_active = n;
    for (size_t i = 0; i < n; ++i) { if (p.flags[i] & PF_ACTIVE) { first_active = i; break; } }
    if (first_active == n) return;  // nenhuma partícula activa

    double xmin = p.x[first_active], xmax = p.x[first_active];
    double ymin = p.y[first_active], ymax = p.y[first_active];
    for (size_t i = first_active + 1; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        if (p.x[i] < xmin) xmin = p.x[i];
        if (p.x[i] > xmax) xmax = p.x[i];
        if (p.y[i] < ymin) ymin = p.y[i];
        if (p.y[i] > ymax) ymax = p.y[i];
    }
    // Usar ~2% da extensão máxima como tamanho base do billboard
    float spread = static_cast<float>(std::max({xmax - xmin, ymax - ymin, 1e-10}));
    float base_sz = std::max(spread * 0.02f, 1e-6f);

    for (size_t i = 0; i < n; ++i) {
        if (!(p.flags[i] & PF_ACTIVE)) continue;
        float rx = static_cast<float>(p.x[i] - cam_world_pos_.x);
        float ry = static_cast<float>(p.y[i] - cam_world_pos_.y);
        float rz = static_cast<float>(p.z[i] - cam_world_pos_.z);
        float camera_dist = std::sqrt(rx * rx + ry * ry + rz * rz);
        float particle_sz = std::max(base_sz, camera_dist * 0.0035f);
        pos_data.push_back(rx); pos_data.push_back(ry);
        pos_data.push_back(rz); pos_data.push_back(particle_sz);
        col_data.push_back(p.color_r[i] * p.luminosity[i]);
        col_data.push_back(p.color_g[i] * p.luminosity[i]);
        col_data.push_back(p.color_b[i] * p.luminosity[i]);
        col_data.push_back(1.0f);
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
    glUniformMatrix4fv(glGetUniformLocation(particle_shader_.id, "u_view"),
                       1, GL_FALSE, glm::value_ptr(view_mat_));
    glUniformMatrix4fv(glGetUniformLocation(particle_shader_.id, "u_proj"),
                       1, GL_FALSE, glm::value_ptr(proj_mat_));
    glUniform1f(glGetUniformLocation(particle_shader_.id, "u_opacity"), render_opacity_);

    glBindVertexArray(particle_vao_.id);
    // Desativar escrita de profundidade: partículas transparentes não devem ocluir
    // umas às outras via depth buffer (apenas partículas atrás de geometria sólida seriam
    // descartadas, mas aqui não há geometria sólida).
    glDepthMask(GL_FALSE);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(draw_count));
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// ── Volume field rendering (raymarched density) ───────────────────────────────

void Renderer::renderVolumeField(const Universe& universe) {
    const GridData& field = universe.density_field;
    if (field.data.empty() || !volume_shader_.id) return;

    // Enviar campo para textura 3D (redimensionar se necessário)
    glBindTexture(GL_TEXTURE_3D, density_3d_tex_.id);
    if (field.NX > 0) {
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F,
                     field.NX, field.NY, field.NZ, 0,
                     GL_RED, GL_FLOAT, field.data.data());
    }

    glUseProgram(volume_shader_.id);
    glUniform1i(glGetUniformLocation(volume_shader_.id, "u_density_tex"), 0);
    glUniform1f(glGetUniformLocation(volume_shader_.id, "u_density_scale"), 300.0f);
    glUniform1f(glGetUniformLocation(volume_shader_.id, "u_opacity_scale"), 15.0f);
    glUniform1f(glGetUniformLocation(volume_shader_.id, "u_opacity"), render_opacity_);
    glUniformMatrix4fv(glGetUniformLocation(volume_shader_.id, "u_inv_view_proj"),
                       1, GL_FALSE,
                       glm::value_ptr(glm::inverse(proj_mat_ * view_mat_)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, density_3d_tex_.id);

    glBindVertexArray(quad_vao_.id);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

// ── Exibição de abundâncias nucleares ──────────────────────────────────────────────────
// Tratado inteiramente pelo ImGui em RegimeOverlay. Nada a desenhar no GL aqui.
void Renderer::renderNuclearAbundances(const NuclearAbundances& /*ab*/) {}

// ── Flash do CMB ──────────────────────────────────────────────────────────────────

void Renderer::renderCMBFlash(float t) {
    // Bloom gaussiano brilhante que se dissipa — renderizado como um quad de tela cheia
    if (!tonemap_shader_.id) return;
    // Usar blend aditivo, desenhar um quad branco com alpha = flash_t
    float alpha = std::max(0.0f, 1.0f - t) * 5.0f;  // muito brilhante no início
    glUseProgram(tonemap_shader_.id);
    glUniform1f(glGetUniformLocation(tonemap_shader_.id, "u_cmb_flash"), alpha);
    glBindVertexArray(quad_vao_.id);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

// ── Halos de galáxias (esferas em wireframe) ───────────────────────────────────────
void Renderer::renderGalaxyHalos(const HaloInfo* /*halos*/, int /*count*/) {
    // TODO Fase 2: desenhar contornos de halos FoF
}

// ── Passagem de pós-processamento ──────────────────────────────────────────────────

void Renderer::applyPostProcess() {
    // Bloom: limiar → desfoque → composição
    // Por enquanto: blit simples do framebuffer HDR para o padrão com mapeamento de tons ACES

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    if (!tonemap_shader_.id) {
        // Fallback: apenas blit
        glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        return;
    }

    glUseProgram(tonemap_shader_.id);
    glUniform1i(glGetUniformLocation(tonemap_shader_.id, "u_hdr_tex"), 0);
    glUniform1f(glGetUniformLocation(tonemap_shader_.id, "u_exposure"), 1.0f);
    glUniform1f(glGetUniformLocation(tonemap_shader_.id, "u_cmb_flash"), 0.0f);
    glUniform1f(glGetUniformLocation(tonemap_shader_.id, "u_blend_t"), blend_t_);

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
