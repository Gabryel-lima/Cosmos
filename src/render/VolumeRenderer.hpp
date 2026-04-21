#pragma once
// src/render/VolumeRenderer.hpp — Renderizador de campo de densidade 3D por raymarching.
#include <glad/gl.h>
#include <glm/glm.hpp>

struct GridData;

class VolumeRenderer {
public:
    bool init();
    void render(const GridData& field, const glm::mat4& inv_view_proj,
                float density_scale = 1.0f, float opacity_scale = 0.5f);
    void setColorTint(float r, float g, float b);

private:
    GLuint program_  = 0;
    GLuint vao_      = 0;
    GLuint quad_vbo_ = 0;
    GLuint tex3d_    = 0;
    float  tint_[3]  = {1.0f, 0.8f, 0.5f};
};
