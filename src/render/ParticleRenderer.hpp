#pragma once
// src/render/ParticleRenderer.hpp — Helper de renderização de billboards instanciados.
// Pertence ao Renderer; pode ser usado de forma independente para efeitos específicos.
#include <glad/gl.h>
#include <glm/glm.hpp>

struct ParticlePool;

class ParticleRenderer {
public:
    bool init();
    void render(const ParticlePool& pool, const glm::mat4& view, const glm::mat4& proj,
                const glm::dvec3& cam_world_pos, float particle_scale = 1.0f);
    void setColorOverride(float r, float g, float b, float a);
    void clearColorOverride();

private:
    GLuint program_   = 0;
    GLuint vao_       = 0;
    GLuint quad_vbo_  = 0;
    GLuint pos_ssbo_  = 0;
    GLuint col_ssbo_  = 0;
    bool   use_color_override_ = false;
    float  color_override_[4]  = {1,1,1,1};
};
