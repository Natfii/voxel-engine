/**
 * @file center_of_gravity.cpp
 * @brief Center of gravity physics implementation
 */

#include "player_physics/center_of_gravity.h"
#include "animation/skeleton_animator.h"
#include "logger.h"

namespace PlayerPhysics {

CenterOfGravity::CenterOfGravity()
    : m_position(0.0f)
    , m_velocity(0.0f)
    , m_accumulatedForce(0.0f)
    , m_mass(1.0f)
    , m_skeleton(nullptr)
    , m_spineRootIndex(-1)
{
}

void CenterOfGravity::initialize(const RuntimeSkeleton* skeleton) {
    m_skeleton = skeleton;

    if (skeleton) {
        m_spineRootIndex = skeleton->findBone("spine_root");
        if (m_spineRootIndex < 0) {
            Logger::warning() << "CenterOfGravity: spine_root bone not found";
        } else {
            Logger::info() << "CenterOfGravity: Initialized at spine_root (bone " << m_spineRootIndex << ")";
        }
    }

    reset();
}

void CenterOfGravity::applyForce(const glm::vec3& force) {
    m_accumulatedForce += force;
}

void CenterOfGravity::applyImpulse(const glm::vec3& impulse) {
    // Impulse directly changes velocity (F*dt = m*dv, so dv = impulse/m)
    m_velocity += impulse / m_mass;
}

void CenterOfGravity::reset() {
    m_position = glm::vec3(0.0f);
    m_velocity = glm::vec3(0.0f);
    m_accumulatedForce = glm::vec3(0.0f);
}

} // namespace PlayerPhysics
