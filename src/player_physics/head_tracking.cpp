/**
 * @file head_tracking.cpp
 * @brief Procedural head tracking implementation
 */

#define GLM_ENABLE_EXPERIMENTAL

#include "player_physics/head_tracking.h"
#include "animation/skeleton_animator.h"
#include "logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>

namespace PlayerPhysics {

HeadTracking::HeadTracking()
    : m_skeleton(nullptr)
    , m_enabled(true)
    , m_headBoneIndex(-1)
    , m_neckBoneIndex(-1)
    , m_lookDirection(0.0f, 0.0f, -1.0f)
    , m_bodyForward(0.0f, 0.0f, -1.0f)
    , m_bodyUp(0.0f, 1.0f, 0.0f)
    , m_currentYaw(0.0f)
    , m_currentPitch(0.0f)
    , m_targetYaw(0.0f)
    , m_targetPitch(0.0f)
    , m_currentHeadRotation(1.0f, 0.0f, 0.0f, 0.0f)
    , m_currentNeckRotation(1.0f, 0.0f, 0.0f, 0.0f)
{
}

HeadTracking::~HeadTracking() = default;

void HeadTracking::initialize(const RuntimeSkeleton* skeleton) {
    m_skeleton = skeleton;

    if (!skeleton) {
        Logger::warning() << "HeadTracking: No skeleton provided";
        return;
    }

    // Find head and neck bones
    m_headBoneIndex = skeleton->findBone("head");
    m_neckBoneIndex = skeleton->findBone("spine_tip");

    if (m_headBoneIndex < 0) {
        Logger::warning() << "HeadTracking: 'head' bone not found";
    }
    if (m_neckBoneIndex < 0 && m_params.enableNeckBlend) {
        Logger::warning() << "HeadTracking: 'spine_tip' bone not found for neck blending";
    }

    Logger::info() << "HeadTracking: Initialized (head=" << m_headBoneIndex
                   << ", neck=" << m_neckBoneIndex << ")";

    reset();
}

void HeadTracking::setLookDirection(const glm::vec3& direction) {
    if (glm::length(direction) > 0.0001f) {
        m_lookDirection = glm::normalize(direction);
    }
}

void HeadTracking::setBodyForward(const glm::vec3& forward) {
    if (glm::length(forward) > 0.0001f) {
        m_bodyForward = glm::normalize(forward);
    }
}

void HeadTracking::setBodyUp(const glm::vec3& up) {
    if (glm::length(up) > 0.0001f) {
        m_bodyUp = glm::normalize(up);
    }
}

void HeadTracking::update(float deltaTime) {
    if (!m_enabled) {
        // Smoothly return to neutral when disabled
        m_targetYaw = 0.0f;
        m_targetPitch = 0.0f;
    } else {
        calculateTargetAngles();
    }

    interpolateAngles(deltaTime);
    buildRotations();
}

void HeadTracking::calculateTargetAngles() {
    // Calculate look direction relative to body orientation
    // Body right vector
    glm::vec3 bodyRight = glm::normalize(glm::cross(m_bodyForward, m_bodyUp));

    // Project look direction onto body horizontal plane for yaw
    glm::vec3 lookHorizontal = m_lookDirection;
    lookHorizontal.y = 0.0f;
    if (glm::length(lookHorizontal) > 0.0001f) {
        lookHorizontal = glm::normalize(lookHorizontal);
    } else {
        lookHorizontal = m_bodyForward;
    }

    // Calculate yaw angle (horizontal rotation)
    float dotForward = glm::dot(lookHorizontal, m_bodyForward);
    float dotRight = glm::dot(lookHorizontal, bodyRight);
    m_targetYaw = glm::degrees(std::atan2(dotRight, dotForward));

    // Calculate pitch angle (vertical rotation)
    m_targetPitch = glm::degrees(std::asin(glm::clamp(m_lookDirection.y, -1.0f, 1.0f)));

    // Clamp to limits
    m_targetYaw = glm::clamp(m_targetYaw, -m_params.maxYawAngle, m_params.maxYawAngle);
    m_targetPitch = glm::clamp(m_targetPitch, -m_params.maxPitchAngle, m_params.maxPitchAngle);
}

void HeadTracking::interpolateAngles(float deltaTime) {
    // Smooth interpolation toward target angles
    float speed = m_enabled ? m_params.trackingSpeed : m_params.returnSpeed;

    // Exponential smoothing
    float factor = 1.0f - std::exp(-speed * deltaTime);

    m_currentYaw = glm::mix(m_currentYaw, m_targetYaw, factor);
    m_currentPitch = glm::mix(m_currentPitch, m_targetPitch, factor);
}

void HeadTracking::buildRotations() {
    // Build head rotation quaternion
    // Rotate around Y for yaw (horizontal), then X for pitch (vertical)
    float headYaw = glm::radians(m_currentYaw * m_params.blendWeight);
    float headPitch = glm::radians(m_currentPitch * m_params.blendWeight);

    glm::quat yawRot = glm::angleAxis(headYaw, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::quat pitchRot = glm::angleAxis(headPitch, glm::vec3(1.0f, 0.0f, 0.0f));

    m_currentHeadRotation = yawRot * pitchRot;

    // Build neck rotation (partial rotation for more natural look)
    if (m_params.enableNeckBlend) {
        float neckYaw = headYaw * m_params.neckBlendRatio;
        float neckPitch = headPitch * m_params.neckBlendRatio;

        glm::quat neckYawRot = glm::angleAxis(neckYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat neckPitchRot = glm::angleAxis(neckPitch, glm::vec3(1.0f, 0.0f, 0.0f));

        m_currentNeckRotation = neckYawRot * neckPitchRot;
    } else {
        m_currentNeckRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
}

void HeadTracking::applyToAnimator(SkeletonAnimator& animator) {
    if (!m_skeleton || !m_enabled) return;

    RuntimeSkeleton* skeleton = animator.getSkeleton();
    if (!skeleton) return;

    // Apply head rotation
    if (m_headBoneIndex >= 0 && m_headBoneIndex < static_cast<int>(skeleton->bones.size())) {
        skeleton->bones[m_headBoneIndex].animRotation =
            skeleton->bones[m_headBoneIndex].animRotation * m_currentHeadRotation;
    }

    // Apply neck rotation
    if (m_params.enableNeckBlend &&
        m_neckBoneIndex >= 0 && m_neckBoneIndex < static_cast<int>(skeleton->bones.size())) {
        skeleton->bones[m_neckBoneIndex].animRotation =
            skeleton->bones[m_neckBoneIndex].animRotation * m_currentNeckRotation;
    }
}

void HeadTracking::reset() {
    m_currentYaw = 0.0f;
    m_currentPitch = 0.0f;
    m_targetYaw = 0.0f;
    m_targetPitch = 0.0f;
    m_currentHeadRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    m_currentNeckRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

} // namespace PlayerPhysics
