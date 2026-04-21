// src/core/Camera.cpp
#include "Camera.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

void Camera::processMouseDelta(float dx, float dy) {
    constexpr float SENSITIVITY = 0.15f;
    yaw_   += dx * SENSITIVITY;
    pitch_ -= dy * SENSITIVITY;
    pitch_  = std::clamp(pitch_, -89.0f, 89.0f);
    rebuildForwardFromAngles();
}

void Camera::processScroll(float delta) {
    // Zoom logarítmico: cada clique multiplica/divide zoom_distance
    constexpr double ZOOM_FACTOR = 0.85;
    if (delta > 0.0f)
        zoom_distance *= ZOOM_FACTOR;
    else if (delta < 0.0f)
        zoom_distance /= ZOOM_FACTOR;
    zoom_distance = std::clamp(zoom_distance, DIST_ATOMIC * 1e-3, DIST_COSMIC * 1e3);
    move_speed_ = static_cast<float>(zoom_distance) * 0.3f;
    updateMode();
}

void Camera::processKeyboard(const InputState& in, float dt) {
    float speed = move_speed_ * dt;
    glm::vec3 right = glm::normalize(glm::cross(forward, up));

    glm::dvec3 delta(0.0);
    if (in.w) delta += glm::dvec3(forward)  * static_cast<double>(speed);
    if (in.s) delta -= glm::dvec3(forward)  * static_cast<double>(speed);
    if (in.a) delta -= glm::dvec3(right)    * static_cast<double>(speed);
    if (in.d) delta += glm::dvec3(right)    * static_cast<double>(speed);
    if (in.q) { // girar à esquerda
        up = glm::mat3(glm::rotate(glm::mat4(1.0f),  glm::radians(45.0f * dt), forward)) * up;
    }
    if (in.e) { // girar à direita
        up = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(-45.0f * dt), forward)) * up;
    }
    position += delta;

    if (in.tab_pressed) {
        // Ciclar modo manualmente
        int m = (static_cast<int>(mode) + 1) % 4;
        mode = static_cast<Mode>(m);
    }
    if (in.esc_pressed) releaseTracking();
    if (in.c_pressed) { /* tratado externamente via applyState() */ }
}

void Camera::trackParticle(uint32_t particle_id) {
    tracked_id = particle_id;
    orbiting_  = true;
}

void Camera::releaseTracking() {
    tracked_id = 0;
    orbiting_  = false;
}

void Camera::updateTracking(glm::dvec3 target_world_pos) {
    if (!orbiting_) return;
    look_at_target_ = target_world_pos;
    // Manter câmera a zoom_distance do alvo, preservando direção forward
    glm::dvec3 dir = glm::dvec3(forward);
    if (glm::length(dir) < 1e-6) dir = {0, 0, -1};
    position = look_at_target_ - dir * zoom_distance;
}

void Camera::updateMode() {
    if      (zoom_distance >= DIST_COSMIC)   mode = Mode::COSMIC;
    else if (zoom_distance >= DIST_GALACTIC) mode = Mode::GALACTIC;
    else if (zoom_distance >= DIST_STELLAR)  mode = Mode::STELLAR;
    else                                     mode = Mode::ATOMIC;
}

glm::mat4 Camera::getViewMatrix() const {
    // Câmera relativa: converter posição double para offset float
    glm::vec3 pos_f = glm::vec3(0.0f); // sempre na origem no espaço relativo à câmera
    glm::vec3 target_f = pos_f + forward;
    return glm::lookAt(pos_f, target_f, up);
}

glm::mat4 Camera::getProjectionMatrix(float aspect) const {
    if (ortho_mode) {
        float h = ortho_size;
        float w = h * aspect;
        return glm::ortho(-w, w, -h, h, -100.0f, 100.0f);
    }
    return glm::perspective(glm::radians(fov_deg), aspect, 1e-6f, 1e10f);
}

void Camera::applyState(const State& s) {
    position      = s.position;
    forward       = s.forward;
    zoom_distance = s.zoom_distance;
    ortho_mode    = s.ortho_mode;
    updateMode();
    // Recalcular yaw/pitch a partir de forward
    pitch_ = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
    yaw_   = glm::degrees(std::atan2(forward.z, forward.x));
}

Camera::State Camera::getRegimeDefaultState(int regime_index) const {
    switch (regime_index) {
        case 0: return { {0,0,1},   {0,0,-1}, 1.0,          true  }; // ortho 2D
        case 1: return { {0,0,5},   {0,0,-1}, 3.0,          false };
        case 2: return { {0,0,5},   {0,0,-1}, 3.0,          false };
        case 3: return { {0,0,200}, {0,0,-1}, DIST_COSMIC,  false };
        case 4: return { {0,0,200}, {0,0,-1}, DIST_COSMIC,  false };
        default:return { {0,0,5},   {0,0,-1}, 5.0,          false };
    }
}

void Camera::rebuildForwardFromAngles() {
    glm::vec3 f;
    f.x = std::cos(glm::radians(yaw_)) * std::cos(glm::radians(pitch_));
    f.y = std::sin(glm::radians(pitch_));
    f.z = std::sin(glm::radians(yaw_)) * std::cos(glm::radians(pitch_));
    forward = glm::normalize(f);
}
