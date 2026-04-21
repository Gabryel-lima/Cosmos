// src/render/VolumeRenderer.cpp
#include "VolumeRenderer.hpp"
#include "../core/Universe.hpp"

bool VolumeRenderer::init() { return true; }

void VolumeRenderer::render(const GridData& /*field*/,
                             const glm::mat4& /*inv_view_proj*/,
                             float /*density_scale*/, float /*opacity_scale*/)
{
    // A renderização volumétrica está implementada em Renderer::renderVolumeField()
    // Este stub é mantido para possível uso independente.
}

void VolumeRenderer::setColorTint(float r, float g, float b) {
    tint_[0] = r; tint_[1] = g; tint_[2] = b;
}
