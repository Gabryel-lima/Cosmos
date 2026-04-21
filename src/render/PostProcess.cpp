// src/render/PostProcess.cpp
#include "PostProcess.hpp"

bool PostProcess::init(int w, int h) {
    width_ = w; height_ = h;
    return true;
}

void PostProcess::resize(int w, int h) {
    width_ = w; height_ = h;
}

void PostProcess::apply(GLuint /*hdr_tex*/, float /*bloom_strength*/, float /*exposure*/) {
    // A implementação completa está em Renderer::applyPostProcess()
    // Este stub está aqui pelo contrato do cabeçalho.
}

void PostProcess::setRegimeLUT(int regime_index) {
    regime_lut_ = regime_index;
}
