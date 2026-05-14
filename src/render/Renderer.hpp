#pragma once
// src/render/Renderer.hpp — Gerencia todo o estado OpenGL e orquestra as passagens de renderização.
// Todo o gerenciamento de recursos GL passa pelos wrappers RAII definidos aqui.

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include "ICosmicRenderer.hpp"
#include "GasSplatRenderer.hpp"
#include "StarGlowRenderer.hpp"
#include "StromgrenRenderer.hpp"
#include "StarFormationFX.hpp"
#include "FilamentRenderer.hpp"

struct Universe;
struct NuclearAbundances;
class  Camera;

// ── Wrappers RAII para objetos GL ────────────────────────────────────────────

struct GlBuffer {
    GLuint id = 0;
    GlBuffer() = default;
    ~GlBuffer() { if (id) glDeleteBuffers(1, &id); }
    GlBuffer(const GlBuffer&)            = delete;
    GlBuffer& operator=(const GlBuffer&) = delete;
    GlBuffer(GlBuffer&& o) noexcept : id(o.id) { o.id = 0; }
};

struct GlTexture {
    GLuint id = 0;
    GlTexture() = default;
    ~GlTexture() { if (id) glDeleteTextures(1, &id); }
    GlTexture(const GlTexture&)            = delete;
    GlTexture& operator=(const GlTexture&) = delete;
    GlTexture(GlTexture&& o) noexcept : id(o.id) { o.id = 0; }
};

struct GlVAO {
    GLuint id = 0;
    GlVAO() = default;
    ~GlVAO() { if (id) glDeleteVertexArrays(1, &id); }
    GlVAO(const GlVAO&)            = delete;
    GlVAO& operator=(const GlVAO&) = delete;
};

struct GlFBO {
    GLuint id = 0;
    GlFBO() = default;
    ~GlFBO() { if (id) glDeleteFramebuffers(1, &id); }
    GlFBO(const GlFBO&)            = delete;
    GlFBO& operator=(const GlFBO&) = delete;
};

struct GlShader {
    GLuint id = 0;
    GlShader() = default;
    ~GlShader() { if (id) glDeleteProgram(id); }
    GlShader(const GlShader&) = delete;
    GlShader(GlShader&& o) noexcept : id(o.id) { o.id = 0; }
};

// ── Dados de halo para renderização de galáxias ─────────────────────────────
struct HaloInfo {
    double cx, cy, cz;
    double mass;
    double radius;
    double emissivity;
    float  gas_fraction;
    int    member_count;
};

struct RendererDiagnostics {
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    int bloom_width = 0;
    int bloom_height = 0;
    int density_tex_nx = 0;
    int density_tex_ny = 0;
    int density_tex_nz = 0;
    int ionization_tex_nx = 0;
    int ionization_tex_ny = 0;
    int ionization_tex_nz = 0;
    int emissivity_tex_nx = 0;
    int emissivity_tex_ny = 0;
    int emissivity_tex_nz = 0;
    int inflation_tex_width = 0;
    int inflation_tex_height = 0;
    float last_gpu_ms = 0.0f;
    std::size_t last_particle_draw_count = 0;
    int last_halo_draw_count = 0;
    int last_volume_grid_nx = 0;
    int last_volume_grid_ny = 0;
    int last_volume_grid_nz = 0;
    GLuint particle_shader_id = 0;
    GLuint volume_shader_id = 0;
    GLuint inflation_shader_id = 0;
    GLuint tonemap_shader_id = 0;
    GLuint bloom_threshold_shader_id = 0;
    GLuint bloom_blur_shader_id = 0;
    GLuint hdr_color_tex_id = 0;
    GLuint hdr_depth_tex_id = 0;
    GLuint bloom_tex0_id = 0;
    GLuint bloom_tex1_id = 0;
    GLuint density_tex_id = 0;
    GLuint ionization_tex_id = 0;
    GLuint emissivity_tex_id = 0;
    GLuint inflation_tex_id = 0;
    GLuint particle_pos_ssbo_id = 0;
    GLuint particle_col_ssbo_id = 0;
};

// ── Renderizador principal ────────────────────────────────────────────────────
class Renderer {
public:
    Renderer();
    ~Renderer() = default;

    /// Inicializa todos os recursos GL. Chamar após a criação do contexto GL.
    bool init(int width, int height);

    /// Redimensiona os framebuffers ao redimensionar a janela.
    void resize(int width, int height);

    /// Recarrega todos os shaders do disco (tecla R).
    void reloadShaders();

    /// Libera recursos GL explicitamente antes de destruir o contexto.
    void shutdown();

    /// Chamado no início do quadro.
    void beginFrame();

    /// Chamado no final do quadro (envia para o FBO padrão).
    void endFrame();

    // ── Comandos de renderização chamados pelos Regimes ──────────────────────────
    void renderInflationField(const Universe& universe);
    void renderParticles(const Universe& universe);
    void renderVolumeField(const Universe& universe);
    void renderNuclearAbundances(const NuclearAbundances& ab);
    void renderCMBFlash(float t);  // t: 0→1 progresso do flash
    void renderGalaxyHalos(const HaloInfo* halos, int count);

    // ── Late-Regime FX (Regimes 6–8) ───────────────────────────────
    void renderGasSplat(const Universe& universe, const Camera& cam);
    void renderStarGlow(const Universe& universe, const Camera& cam);
    void renderStromgren(const Universe& universe, const Camera& cam);
    void renderStarFX(const Universe& universe, const Camera& cam);
    void renderFilaments(const Universe& universe, const Camera& cam);

    /// Define as matrizes (chamado pelo laço principal após atualização da câmera).
    void setViewProjection(const glm::mat4& view, const glm::mat4& proj,
                           const glm::dvec3& cam_world_pos);

    /// Define o blend de regime (0..1) para o crossfade de transição.
    void setRegimeBlend(int from_regime, int to_regime, float blend_t);
    void setRenderOpacity(float opacity);

    // Para RegimeOverlay (ImGui): estatísticas somente leitura
    int   getWidth()  const { return width_; }
    int   getHeight() const { return height_; }

    // Temporizador GPU
    float getLastFrameGpuMs() const { return last_gpu_ms_; }
    RendererDiagnostics collectDiagnostics() const;

private:
    bool loadShaderProgram(GlShader& prog,
                           const std::string& vert_path,
                           const std::string& frag_path);
    bool loadComputeShader(GlShader& prog, const std::string& comp_path);
    GLuint compileShader(GLenum type, const std::string& path);
    void setupParticleBuffers();
    void setupQuadBuffers();
    void setupFBOs();
    void setupLookupTextures();
    void applyPostProcess();
    void setVec3Uniform(GLuint program, const char* name, const glm::vec3& value) const;
    bool loadPgmTexture2D(GlTexture& texture,
                          const std::filesystem::path& relative_path,
                          int& out_width,
                          int& out_height);
    void syncVisualTuning(const Universe& universe);

    int width_ = 1280, height_ = 720;
    int bloom_width_ = 640;
    int bloom_height_ = 360;
    float last_gpu_ms_ = 0.0f;
    float cmb_flash_alpha_ = 0.0f; // Armazena a intensidade do flash CMB para o passe de pós-processamento
    std::size_t last_particle_draw_count_ = 0;
    int last_halo_draw_count_ = 0;
    int last_volume_grid_nx_ = 0;
    int last_volume_grid_ny_ = 0;
    int last_volume_grid_nz_ = 0;
    int density_tex_nx_ = 64;
    int density_tex_ny_ = 64;
    int density_tex_nz_ = 64;
    int ionization_tex_nx_ = 64;
    int ionization_tex_ny_ = 64;
    int ionization_tex_nz_ = 64;
    int emissivity_tex_nx_ = 64;
    int emissivity_tex_ny_ = 64;
    int emissivity_tex_nz_ = 64;
    int inflation_tex_width_ = 256;
    int inflation_tex_height_ = 256;

    // Dados da câmera
    glm::mat4   view_mat_      = glm::mat4(1.0f);
    glm::mat4   proj_mat_      = glm::mat4(1.0f);
    glm::dvec3  cam_world_pos_ = {};

    // Estado de transição
    int   blend_from_  = 0;
    int   blend_to_    = 0;
    float blend_t_     = 0.0f;
    int   current_regime_ = 0;
    float render_opacity_ = 1.0f;
    float exposure_multiplier_ = 1.0f;
    float volume_opacity_multiplier_ = 1.0f;
    float cmb_flash_strength_ = 1.0f;
    float halo_visibility_ = 1.0f;
    float halo_axis_ratio_ = 1.28f;
    bool  halos_enabled_ = true;
    // ── Late-Regime FX renderers (regimes 6–8) ────────────────────────
    GasSplatRenderer   gas_splat_renderer_;
    StarGlowRenderer   star_glow_renderer_;
    StromgrenRenderer  stromgren_renderer_;
    StarFormationFX    star_fx_renderer_;
    FilamentRenderer   filament_renderer_;
    QualityTier        current_quality_ = QualityTier::MEDIUM;
    // Shaders
    GlShader particle_shader_;
    GlShader volume_shader_;
    GlShader inflation_shader_;
    GlShader tonemap_shader_;
    GlShader bloom_threshold_shader_;
    GlShader bloom_blur_shader_;

    // Buffers de partículas (SSBO)
    GlBuffer particle_pos_ssbo_;
    GlBuffer particle_col_ssbo_;
    GlVAO    particle_vao_;
    GlBuffer particle_vbo_;

    // Quad de tela cheia
    GlVAO    quad_vao_;
    GlBuffer quad_vbo_;

    // Framebuffer HDR
    GlFBO    hdr_fbo_;
    GlTexture hdr_color_tex_;
    GlTexture hdr_depth_tex_;
    GlFBO    bloom_fbo_[2];
    GlTexture bloom_tex_[2];

    // Textura de densidade 3D (atualizada a partir dos dados de campo)
    GlTexture density_3d_tex_;
    GlTexture ionization_3d_tex_;
    GlTexture emissivity_3d_tex_;
    GlTexture volume_macro_lookup_dark_ages_tex_;
    GlTexture volume_macro_lookup_reionization_tex_;
    GlTexture volume_macro_lookup_structure_tex_;
    // Textura 2D do campo de inflação
    GlTexture inflation_2d_tex_;
    std::array<int, 3> volume_macro_lookup_width_ = {1, 1, 1};
    std::array<int, 3> volume_macro_lookup_height_ = {1, 1, 1};
    std::array<bool, 3> volume_macro_lookup_loaded_ = {false, false, false};

    // Consultas do temporizador GPU
    GLuint timer_query_[2] = {0, 0};
    int    timer_idx_      = 0;
    bool   timer_history_ready_ = false;
};
