/**
 * @file squish_system.cpp
 * @brief Squish/deformation system implementation
 */

#include "player_physics/squish_system.h"
#include "animation/skeleton_animator.h"
#include "logger.h"

#include <cmath>
#include <algorithm>

namespace PlayerPhysics {

SquishSystem::SquishSystem()
    : m_skeleton(nullptr)
{
}

SquishSystem::~SquishSystem() = default;

void SquishSystem::initialize(const RuntimeSkeleton* skeleton) {
    m_skeleton = skeleton;

    if (!skeleton) {
        Logger::warning() << "SquishSystem: No skeleton provided";
        return;
    }

    // Initialize state for each bone
    m_boneStates.resize(skeleton->bones.size());
    m_boneWorldPositions.resize(skeleton->bones.size(), glm::vec3(0.0f));

    for (auto& state : m_boneStates) {
        state.reset();
    }

    Logger::info() << "SquishSystem: Initialized for " << skeleton->bones.size() << " bones";
}

void SquishSystem::onCollision(const glm::vec3& contactPoint,
                                const glm::vec3& contactNormal,
                                float impactForce) {
    if (!m_skeleton || m_boneStates.empty()) return;

    // Clamp impact force to reasonable range
    impactForce = glm::clamp(impactForce, 0.0f, 20.0f);

    // Apply squish to each bone based on distance from contact
    for (size_t i = 0; i < m_boneStates.size(); ++i) {
        computeBoneSquish(static_cast<int>(i), contactPoint, contactNormal, impactForce);
    }
}

void SquishSystem::computeBoneSquish(int boneIndex,
                                      const glm::vec3& contactPoint,
                                      const glm::vec3& contactNormal,
                                      float impactForce) {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(m_boneStates.size())) return;

    BoneSquishState& state = m_boneStates[boneIndex];
    const glm::vec3& bonePos = m_boneWorldPositions[boneIndex];

    // Calculate distance from contact point
    float distance = glm::distance(bonePos, contactPoint);

    // Calculate influence based on distance (quadratic falloff)
    float normalizedDist = distance / m_params.influenceRadius;
    float influence = 1.0f - glm::clamp(normalizedDist, 0.0f, 1.0f);
    influence = influence * influence;  // Quadratic falloff for smoother edges

    if (influence < 0.01f) return;  // Skip bones too far away

    // Calculate compression amount
    float compression = impactForce * influence * m_params.impactMultiplier;
    compression = glm::clamp(compression, 0.0f, 1.0f - m_params.maxCompression);

    // Calculate target scale
    // Compress along collision normal
    glm::vec3 absNormal = glm::abs(contactNormal);
    glm::vec3 scale = glm::vec3(1.0f);

    // Compress in the direction of the normal
    scale -= absNormal * compression;

    // Volume preservation: expand perpendicular to normal
    glm::vec3 perpendicular = glm::vec3(1.0f) - absNormal;
    scale += perpendicular * compression * m_params.volumePreservation;

    // Clamp scale components
    scale = glm::clamp(scale, glm::vec3(m_params.maxCompression), glm::vec3(m_params.maxExpansion));

    // Set target and reset velocity for spring physics
    state.targetScale = scale;
    state.velocity = glm::vec3(0.0f);
    state.influence = influence;
}

void SquishSystem::update(float deltaTime) {
    if (m_boneStates.empty()) return;

    for (auto& state : m_boneStates) {
        updateBoneSpring(state, deltaTime);
    }
}

void SquishSystem::updateBoneSpring(BoneSquishState& state, float deltaTime) {
    // Spring-damper physics for each axis independently
    // F = -k * displacement - c * velocity
    // Where k = spring stiffness, c = damping coefficient

    for (int axis = 0; axis < 3; ++axis) {
        float displacement = state.currentScale[axis] - state.targetScale[axis];
        float springForce = -m_params.springStiffness * displacement;
        float dampingForce = -m_params.dampingRatio * state.velocity[axis];

        float acceleration = springForce + dampingForce;
        state.velocity[axis] += acceleration * deltaTime;
        state.currentScale[axis] += state.velocity[axis] * deltaTime;

        // Clamp to prevent extreme deformation
        state.currentScale[axis] = glm::clamp(state.currentScale[axis],
                                               m_params.maxCompression,
                                               m_params.maxExpansion);
    }

    // Gradually restore target to rest pose (1.0)
    state.targetScale = glm::mix(state.targetScale, glm::vec3(1.0f),
                                  deltaTime * m_params.recoverySpeed);

    // Decay influence
    state.influence *= 1.0f - deltaTime * 2.0f;
    if (state.influence < 0.01f) state.influence = 0.0f;
}

glm::vec3 SquishSystem::getBoneScale(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(m_boneStates.size())) {
        return glm::vec3(1.0f);
    }
    return m_boneStates[boneIndex].currentScale;
}

void SquishSystem::applyToAnimator(SkeletonAnimator& animator) {
    if (!m_skeleton || m_boneStates.empty()) return;

    // Note: This requires SkeletonAnimator to support physicsScale
    // The scale will be applied in updateBoneTransforms()

    // For now, we can access the skeleton through the animator
    // and apply scales directly to animPosition/animScale

    RuntimeSkeleton* skeleton = const_cast<RuntimeSkeleton*>(animator.getSkeleton());
    if (!skeleton) return;

    for (size_t i = 0; i < m_boneStates.size() && i < skeleton->bones.size(); ++i) {
        const BoneSquishState& state = m_boneStates[i];

        // Only apply if there's meaningful deformation
        if (glm::length(state.currentScale - glm::vec3(1.0f)) > 0.001f) {
            // Multiply with animation scale
            skeleton->bones[i].animScale *= state.currentScale;
        }
    }
}

void SquishSystem::reset() {
    for (auto& state : m_boneStates) {
        state.reset();
    }
}

bool SquishSystem::isDeformed() const {
    for (const auto& state : m_boneStates) {
        if (glm::length(state.currentScale - glm::vec3(1.0f)) > 0.01f) {
            return true;
        }
    }
    return false;
}

void SquishSystem::setBoneWorldPositions(const std::vector<glm::vec3>& positions) {
    m_boneWorldPositions = positions;
}

} // namespace PlayerPhysics
