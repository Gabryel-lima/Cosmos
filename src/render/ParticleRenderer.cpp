// src/render/ParticleRenderer.cpp
#include "ParticleRenderer.hpp"
#include "../physics/ParticlePool.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <vector>
#include <cmath>

bool ParticleRenderer::init() { return true; }

void ParticleRenderer::render(const ParticlePool& pool, const glm::mat4& view,
                               const glm::mat4& proj, const glm::dvec3& cam_world_pos,
                               float particle_scale)
{
    // Delegado para Renderer::renderParticles(); esta classe é um helper para
    // subsistemas futuros que precisem de renderização de partículas independente.
    (void)pool; (void)view; (void)proj; (void)cam_world_pos; (void)particle_scale;
}

void ParticleRenderer::setColorOverride(float r, float g, float b, float a) {
    use_color_override_ = true;
    color_override_[0] = r; color_override_[1] = g;
    color_override_[2] = b; color_override_[3] = a;
}

void ParticleRenderer::clearColorOverride() { use_color_override_ = false; }
