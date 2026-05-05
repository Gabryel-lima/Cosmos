#pragma once
// src/render/ICosmicRenderer.hpp — Interface base para todos os renderers dos Regimes 7+.
//
// CONTRATO (Regra 0.2): todo Render() deve salvar e restaurar o estado OpenGL via GLStateGuard.
// CONTRATO (Regra 0.5): nenhum new/malloc/push_back dentro de Render() ou Update().
// CONTRATO (Regra 0.1): verificar regime antes de qualquer operação.

#include <glad/gl.h>
#include "../physics/ParticlePool.hpp"
#include "../core/Camera.hpp"

// ── Tier de qualidade em tempo de execução ────────────────────────────────────
// Mapeado a partir das macros de build QUALITY_SAFE / QUALITY_MEDIUM / QUALITY_HIGH,
// mas também pode ser ajustado via ImGui em runtime.
enum class QualityTier : int {
    SAFE   = 0,  // ≥ 60 FPS com 730 partículas
    MEDIUM = 1,  // ≥ 45 FPS
    HIGH   = 2,  // ≥ 30 FPS
};

// Determina o tier inicial a partir do símbolo de build.
inline QualityTier DefaultQualityTierFromBuild() {
#if defined(QUALITY_SAFE)
    return QualityTier::SAFE;
#elif defined(QUALITY_HIGH) || defined(QUALITY_ULTRA)
    return QualityTier::HIGH;
#else
    return QualityTier::MEDIUM; // QUALITY_MEDIUM, QUALITY_LOW, ou padrão
#endif
}

// Getter global de conveniência — inicializado em ICosmicRenderer.cpp
QualityTier GetCurrentQuality();
void        SetCurrentQuality(QualityTier tier);

// ── Constantes de regime (indexadas 0–8, igual ao CosmicClock) ────────────────
static constexpr int REGIME_DARK_AGES    = 6;
static constexpr int REGIME_REIONIZATION = 7;
static constexpr int REGIME_STRUCTURE    = 8;

// ── Interface base ────────────────────────────────────────────────────────────
class ICosmicRenderer {
public:
    virtual ~ICosmicRenderer() = default;

    /// Chamado uma vez após contexto OpenGL estar pronto.
    /// Aloca VAO, VBO, shaders, texturas. Retorna false em caso de erro.
    virtual bool Init(QualityTier quality) = 0;

    /// Chamado quando o quality tier muda em runtime.
    /// Deve reconfigurar parâmetros SEM realocar GPU buffers se possível.
    virtual void OnQualityChanged(QualityTier new_quality) = 0;

    /// Chamado todo frame — ZERO alocação (Regra 0.5).
    /// Deve retornar imediatamente se regime < limiar do renderer (Regra 0.1).
    virtual void Render(const ParticlePool& particles,
                        int regime,
                        const Camera& cam,
                        float sim_time_myr) = 0;

    /// Libera todos os recursos OpenGL.
    virtual void Shutdown() = 0;

protected:
    // ── Helper RAII que salva e restaura estado OpenGL (Regra 0.2) ───────────
    struct GLStateGuard {
        GLboolean depth_write  = GL_TRUE;
        GLint     blend_src    = GL_ONE;
        GLint     blend_dst    = GL_ZERO;
        GLboolean blend_enabled = GL_FALSE;
        GLboolean depth_test   = GL_TRUE;
        GLboolean point_size   = GL_FALSE;

        GLStateGuard() {
            glGetBooleanv(GL_DEPTH_WRITEMASK,    &depth_write);
            glGetIntegerv(GL_BLEND_SRC_RGB,      &blend_src);
            glGetIntegerv(GL_BLEND_DST_RGB,      &blend_dst);
            blend_enabled = glIsEnabled(GL_BLEND);
            depth_test    = glIsEnabled(GL_DEPTH_TEST);
            point_size    = glIsEnabled(GL_PROGRAM_POINT_SIZE);
        }

        ~GLStateGuard() {
            glDepthMask(depth_write);
            glBlendFunc(static_cast<GLenum>(blend_src),
                        static_cast<GLenum>(blend_dst));
            blend_enabled ? glEnable(GL_BLEND)      : glDisable(GL_BLEND);
            depth_test    ? glEnable(GL_DEPTH_TEST)  : glDisable(GL_DEPTH_TEST);
            point_size    ? glEnable(GL_PROGRAM_POINT_SIZE)
                          : glDisable(GL_PROGRAM_POINT_SIZE);
        }

        // Não copiável — garante uso correto como variável local de escopo
        GLStateGuard(const GLStateGuard&)            = delete;
        GLStateGuard& operator=(const GLStateGuard&) = delete;
    };
};
