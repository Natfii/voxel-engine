/**
 * @file editor_camera.cpp
 * @brief Implementation of EditorCamera class
 */

#include "editor/editor_camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

// Define GLM_FORCE_RADIANS and GLM_FORCE_DEPTH_ZERO_TO_ONE if not already defined
#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

EditorCamera::EditorCamera()
    : m_position(5.0f, 5.0f, 5.0f)
    , m_target(0.0f, 0.0f, 0.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_yaw(-45.0f)
    , m_pitch(-35.0f)
    , m_distance(glm::length(m_position - m_target))
    , m_fov(45.0f)
    , m_near(0.1f)
    , m_far(1000.0f)
{
    updateCameraVectors();
}

EditorCamera::EditorCamera(const glm::vec3& position, const glm::vec3& target, const glm::vec3& up)
    : m_position(position)
    , m_target(target)
    , m_up(up)
    , m_distance(glm::length(position - target))
    , m_fov(45.0f)
    , m_near(0.1f)
    , m_far(1000.0f)
{
    // Guard against position == target (would cause NaN from normalize)
    if (m_distance < 0.0001f) {
        m_distance = MIN_DISTANCE;
        m_yaw = -45.0f;
        m_pitch = -35.0f;
        updateCameraVectors();
        return;
    }

    // Calculate initial yaw and pitch from position and target
    glm::vec3 direction = glm::normalize(position - target);

    // Yaw: atan2 of x and z components
    m_yaw = glm::degrees(atan2(direction.x, direction.z));

    // Pitch: asin of y component (already normalized)
    // Clamp direction.y to prevent NaN from asin
    float clampedY = glm::clamp(direction.y, -1.0f, 1.0f);
    m_pitch = glm::degrees(asin(clampedY));

    // Clamp pitch to valid range
    m_pitch = glm::clamp(m_pitch, MIN_PITCH, MAX_PITCH);
}

void EditorCamera::updateOrbit(float deltaX, float deltaY) {
    // Update yaw and pitch based on mouse delta
    // deltaX rotates horizontally (yaw)
    // deltaY rotates vertically (pitch)
    m_yaw -= deltaX * ORBIT_SENSITIVITY;
    m_pitch += deltaY * ORBIT_SENSITIVITY;

    // Clamp pitch to prevent gimbal lock
    // Keep camera from flipping upside down
    m_pitch = glm::clamp(m_pitch, MIN_PITCH, MAX_PITCH);

    // Recalculate position from spherical coordinates
    updateCameraVectors();
}

void EditorCamera::setYaw(float yaw) {
    m_yaw = yaw;
    updateCameraVectors();
}

void EditorCamera::setPitch(float pitch) {
    m_pitch = glm::clamp(pitch, MIN_PITCH, MAX_PITCH);
    updateCameraVectors();
}

void EditorCamera::updatePan(float deltaX, float deltaY) {
    // Calculate camera's right and up vectors
    glm::vec3 forwardVec = m_target - m_position;
    float forwardLen = glm::length(forwardVec);

    // Guard against zero-length forward vector
    if (forwardLen < 0.0001f) {
        return;
    }

    glm::vec3 forward = forwardVec / forwardLen;
    glm::vec3 rightVec = glm::cross(forward, m_up);
    float rightLen = glm::length(rightVec);

    // Guard against zero-length cross product (forward parallel to up)
    if (rightLen < 0.0001f) {
        return;
    }

    glm::vec3 right = rightVec / rightLen;
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    // Pan amount is scaled by distance (farther = bigger pan)
    float panScale = m_distance * PAN_SENSITIVITY;

    // Calculate pan offset in screen space
    // Right is positive X, up is positive Y
    glm::vec3 offset = right * (-deltaX * panScale) + up * (deltaY * panScale);

    // Apply offset to both position and target
    // This maintains the camera orientation while translating
    m_position += offset;
    m_target += offset;
}

void EditorCamera::updateZoom(float delta) {
    // Exponential zoom scaling for smooth feel
    // Positive delta = zoom in (decrease distance)
    // Negative delta = zoom out (increase distance)
    m_distance -= delta * ZOOM_SENSITIVITY * m_distance;

    // Clamp distance to prevent inverting through target
    // or zooming too far away
    m_distance = glm::clamp(m_distance, MIN_DISTANCE, MAX_DISTANCE);

    // Recalculate position with new distance
    updateCameraVectors();
}

void EditorCamera::updateFly(float forward, float right, float up, float deltaTime) {
    // Calculate camera's forward, right, and up vectors
    glm::vec3 forwardDir = m_target - m_position;
    float forwardLen = glm::length(forwardDir);

    // Guard against zero-length forward vector
    if (forwardLen < 0.0001f) {
        return;
    }

    glm::vec3 forwardVec = forwardDir / forwardLen;
    glm::vec3 rightDir = glm::cross(forwardVec, m_up);
    float rightLen = glm::length(rightDir);

    // Guard against zero-length cross product
    if (rightLen < 0.0001f) {
        return;
    }

    glm::vec3 rightVec = rightDir / rightLen;
    glm::vec3 upVec = glm::normalize(glm::cross(rightVec, forwardVec));

    // Calculate movement vector in camera-local space
    glm::vec3 movement = forwardVec * forward + rightVec * right + upVec * up;

    // Scale by speed and deltaTime for frame-rate independence
    movement *= FLY_SPEED * deltaTime;

    // Apply movement to both position and target
    // This maintains camera orientation during fly mode
    m_position += movement;
    m_target += movement;

    // Update distance calculation (should remain constant in fly mode)
    m_distance = glm::length(m_position - m_target);
}

void EditorCamera::frameBounds(const glm::vec3& min, const glm::vec3& max) {
    // Calculate bounding box center (becomes new target)
    m_target = (min + max) * 0.5f;

    // Calculate bounding box dimensions
    glm::vec3 size = max - min;

    // Calculate bounding sphere radius
    // Use diagonal length to ensure entire box fits in view
    float radius = glm::length(size) * 0.5f;

    // Guard against zero-size bounding box
    if (radius < 0.0001f) {
        radius = 1.0f;  // Default to unit sphere
    }

    // Calculate distance needed to fit bounding sphere in view
    // Distance = radius / tan(fov/2)
    // Add extra margin factor (1.5x) for comfortable framing
    float fovRadians = glm::radians(m_fov);
    float tanHalfFov = tan(fovRadians * 0.5f);

    // Guard against zero tan (should never happen for reasonable FOV)
    float distance;
    if (tanHalfFov < 0.0001f) {
        distance = radius * 10.0f;  // Fallback for near-zero FOV
    } else {
        distance = (radius / tanHalfFov) * 1.5f;
    }

    // Clamp distance to valid range
    m_distance = glm::clamp(distance, MIN_DISTANCE, MAX_DISTANCE);

    // Set default viewing angles for pleasant 3/4 view
    // 45° from front (SW direction), 35° elevation
    m_yaw = -45.0f;
    m_pitch = -35.0f;

    // Calculate new camera position
    updateCameraVectors();
}

glm::mat4 EditorCamera::getViewMatrix() const {
    // Standard lookAt matrix: transforms world space to camera space
    // lookAt(eye, target, up) creates a view matrix looking from
    // eye position toward target position, with up vector defining
    // camera orientation
    return glm::lookAt(m_position, m_target, m_up);
}

glm::mat4 EditorCamera::getProjectionMatrix(float aspectRatio) const {
    // Create perspective projection matrix
    // fov: field of view in radians
    // aspectRatio: width / height
    // near/far: clip plane distances
    glm::mat4 projection = glm::perspective(
        glm::radians(m_fov),
        aspectRatio,
        m_near,
        m_far
    );

    // Apply Vulkan Y-flip correction
    // Vulkan's NDC has Y pointing down, but GLM assumes OpenGL (Y up)
    // Flipping Y in projection matrix corrects this
    projection[1][1] *= -1.0f;

    return projection;
}

void EditorCamera::updateCameraVectors() {
    // Convert spherical coordinates (yaw, pitch, distance) to Cartesian position
    //
    // Spherical Coordinate System:
    // - Yaw (θ): Horizontal angle around Y-axis (0° = +Z, 90° = +X)
    // - Pitch (φ): Vertical angle from XZ-plane (-90° = -Y, +90° = +Y)
    // - Distance (r): Radius from target point
    //
    // Cartesian Conversion:
    // x = r * cos(pitch) * sin(yaw)
    // y = r * sin(pitch)
    // z = r * cos(pitch) * cos(yaw)

    float yawRadians = glm::radians(m_yaw);
    float pitchRadians = glm::radians(m_pitch);

    // Calculate camera offset from target in spherical coordinates
    glm::vec3 offset;
    offset.x = m_distance * cos(pitchRadians) * sin(yawRadians);
    offset.y = m_distance * sin(pitchRadians);
    offset.z = m_distance * cos(pitchRadians) * cos(yawRadians);

    // Position is target plus offset
    m_position = m_target + offset;
}
