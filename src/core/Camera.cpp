// src/core/Camera.cpp
#include "Camera.hpp"
#include "Universe.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

double regimeFramePadding(int regime_index) {
    switch (regime_index) {
        case 0: return 1.10;
        case 1:
        case 2:
        case 3: return 1.12;
        case 4: return 1.15;
        case 5: return 1.18;
        case 6:
        case 7:
        case 8: return 1.20;
        default: return 1.15;
    }
}

double fitDistanceForSphere(double radius, float fov_deg) {
    double half_fov_rad = glm::radians(static_cast<double>(std::clamp(fov_deg, 20.0f, 100.0f)) * 0.5);
    double sin_half_fov = std::sin(half_fov_rad);
    if (sin_half_fov <= 1e-6) {
        return radius * 2.0;
    }
    return radius / sin_half_fov;
}

} // namespace

void Camera::processMouseDelta(float dx, float dy) {
    if (std::abs(dx) > 0.0f || std::abs(dy) > 0.0f) {
        disableAutoFrame();
    }
    constexpr float SENSITIVITY = 0.15f;
    yaw_   += dx * SENSITIVITY;
    pitch_ -= dy * SENSITIVITY;
    pitch_  = std::clamp(pitch_, -89.0f, 89.0f);
    rebuildForwardFromAngles();
}

void Camera::processScroll(float delta) {
    if (delta != 0.0f) {
        disableAutoFrame();
    }
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
    bool manual_motion = false;

    glm::dvec3 delta(0.0);
    if (in.w) { delta += glm::dvec3(forward)  * static_cast<double>(speed); manual_motion = true; }
    if (in.s) { delta -= glm::dvec3(forward)  * static_cast<double>(speed); manual_motion = true; }
    if (in.a) { delta -= glm::dvec3(right)    * static_cast<double>(speed); manual_motion = true; }
    if (in.d) { delta += glm::dvec3(right)    * static_cast<double>(speed); manual_motion = true; }
    if (in.q) { // girar à esquerda
        up = glm::mat3(glm::rotate(glm::mat4(1.0f),  glm::radians(45.0f * dt), forward)) * up;
        manual_motion = true;
    }
    if (in.e) { // girar à direita
        up = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(-45.0f * dt), forward)) * up;
        manual_motion = true;
    }
    if (manual_motion) {
        disableAutoFrame();
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
    disableAutoFrame();
}

void Camera::releaseTracking() {
    tracked_id = std::numeric_limits<uint32_t>::max();
    orbiting_  = false;
}

void Camera::enableAutoFrame() {
    auto_frame_enabled_ = true;
}

void Camera::disableAutoFrame() {
    auto_frame_enabled_ = false;
}

SceneFrame Camera::estimateSceneFrame(const Universe& universe) {
    const ParticlePool& pp = universe.particles;
    SceneFrame frame;
    size_t active_count = 0;
    glm::dvec3 min_pos(0.0);
    glm::dvec3 max_pos(0.0);
    bool has_bounds = false;

    for (size_t i = 0; i < pp.x.size(); ++i) {
        if (!(pp.flags[i] & PF_ACTIVE)) continue;
        glm::dvec3 pos(pp.x[i], pp.y[i], pp.z[i]);
        if (!has_bounds) {
            min_pos = pos;
            max_pos = pos;
            has_bounds = true;
        } else {
            min_pos = glm::min(min_pos, pos);
            max_pos = glm::max(max_pos, pos);
        }
        ++active_count;
    }

    if (has_bounds) {
        frame.center = (min_pos + max_pos) * 0.5;
    }

    std::vector<double> radii;
    radii.reserve(active_count);
    double max_radius = 0.0;
    for (size_t i = 0; i < pp.x.size(); ++i) {
        if (!(pp.flags[i] & PF_ACTIVE)) continue;
        glm::dvec3 delta(pp.x[i], pp.y[i], pp.z[i]);
        delta -= frame.center;
        double r = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        max_radius = std::max(max_radius, r);
        radii.push_back(r);
    }

    if (!radii.empty()) {
        size_t robust_index = static_cast<size_t>(std::floor((static_cast<double>(radii.size()) - 1.0) * 0.95));
        std::nth_element(radii.begin(), radii.begin() + robust_index, radii.end());
        double robust_radius = radii[robust_index];
        frame.radius = std::max(robust_radius, max_radius * 0.35);
    }

    if (frame.radius <= 0.0 && universe.density_field.NX > 0) {
        frame.radius = static_cast<double>(universe.density_field.NX) * 0.5;
    }

    if (frame.radius <= 0.0) {
        switch (universe.regime_index) {
            case 0: frame.radius = 1.0; break;
            case 1:
            case 2:
            case 3: frame.radius = 1.6; break;
            case 4: frame.radius = 4.0; break;
            case 5: frame.radius = 22.0; break;
            case 6: frame.radius = 30.0; break;
            case 7:
            case 8: frame.radius = 36.0; break;
            default: frame.radius = 5.0; break;
        }
    }

    return frame;
}

Camera::State Camera::getSceneFittedState(int regime_index, const SceneFrame& scene_frame) const {
    Camera::State base = getRegimeDefaultState(regime_index);
    double radius = std::max(scene_frame.radius, 1e-6);

    glm::dvec3 desired_forward = glm::normalize(glm::dvec3(base.forward));
    if (!std::isfinite(desired_forward.x) || !std::isfinite(desired_forward.y) || !std::isfinite(desired_forward.z) ||
        glm::length(desired_forward) < 1e-9) {
        desired_forward = {0.0, 0.0, -1.0};
    }

    base.forward = glm::vec3(desired_forward);
    if (base.ortho_mode) {
        base.zoom_distance = radius * regimeFramePadding(regime_index);
    } else {
        const double fitted_distance = fitDistanceForSphere(radius, fov_deg) * regimeFramePadding(regime_index);
        base.zoom_distance = std::max(fitted_distance, radius * 1.05);
    }
    base.position = scene_frame.center - desired_forward * base.zoom_distance;
    return base;
}

void Camera::updateAutoFrame(int regime_index, const glm::dvec3& scene_center,
                             double scene_radius, float dt) {
    if (!auto_frame_enabled_ || orbiting_) return;

    SceneFrame scene_frame{scene_center, scene_radius};
    Camera::State desired = getSceneFittedState(regime_index, scene_frame);
    double radius = std::max(scene_radius, 1e-6);
    float smooth_t = 1.0f - std::exp(-std::max(dt, 0.0f) * 3.5f);
    zoom_distance += (desired.zoom_distance - zoom_distance) * static_cast<double>(smooth_t);
    zoom_distance = std::max(zoom_distance, desired.zoom_distance * 0.75);
    ortho_mode = desired.ortho_mode;
    ortho_size = static_cast<float>(std::max(zoom_distance, radius * 1.2));
    forward = glm::normalize(glm::mix(forward, desired.forward, smooth_t));
    look_at_target_ = glm::mix(look_at_target_, scene_center, static_cast<double>(smooth_t));
    position = look_at_target_ - glm::dvec3(forward) * zoom_distance;
    move_speed_ = static_cast<float>(zoom_distance) * 0.3f;
    updateMode();
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
    forward       = glm::normalize(s.forward);
    zoom_distance = s.zoom_distance;
    ortho_mode    = s.ortho_mode;
    ortho_size    = ortho_mode ? static_cast<float>(std::max(zoom_distance, 1.0)) : ortho_size;
    look_at_target_ = position + glm::dvec3(forward) * zoom_distance;
    tracked_id = std::numeric_limits<uint32_t>::max();
    orbiting_ = false;
    auto_frame_enabled_ = true;
    move_speed_ = static_cast<float>(zoom_distance) * 0.3f;
    updateMode();
    // Recalcular yaw/pitch a partir de forward
    pitch_ = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
    yaw_   = glm::degrees(std::atan2(forward.z, forward.x));
}

Camera::State Camera::getRegimeDefaultState(int regime_index) const {
    switch (regime_index) {
        case 0: return { {0,0,1},    {0,0,-1}, 1.0,  true  }; // ortho 2D
        case 1: return { {0,0,5},    {0,0,-1}, 3.0,  false };
        case 2: return { {0,0,5},    {0,0,-1}, 3.0,  false };
        case 3: return { {0,0,5.5},  {0,0,-1}, 3.2, false };
        case 4: return { {0,0,7.5},  {0,0,-1}, 7.5, false };
        case 5: return { {0,0,57.0}, {0,0,-1}, 38.0, false };
        case 6: return { {0,0,67.5}, {0,0,-1}, 45.0, false };
        case 7: return { {0,0,78.0}, {0,0,-1}, 52.0, false };
        case 8: return { {0,0,88.0}, {0,0,-1}, 58.0, false };
        default:return { {0,0,5},    {0,0,-1}, 5.0,  false };
    }
}

void Camera::rebuildForwardFromAngles() {
    glm::vec3 f;
    f.x = std::cos(glm::radians(yaw_)) * std::cos(glm::radians(pitch_));
    f.y = std::sin(glm::radians(pitch_));
    f.z = std::sin(glm::radians(yaw_)) * std::cos(glm::radians(pitch_));
    forward = glm::normalize(f);
}
