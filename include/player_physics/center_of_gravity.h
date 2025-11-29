/**
 * @file center_of_gravity.h
 * @brief Center of gravity physics controller for player character
 *
 * Implements physics simulation with the center of gravity at the spine_root bone.
 * All forces (gravity, collision response, momentum) originate from this central point.
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Forward declarations
class World;
class RuntimeSkeleton;

namespace PlayerPhysics {

/**
 * @brief Center of gravity controller for player character
 *
 * Manages physics simulation centered at the spine_root bone position.
 * Forces are applied here and propagate through the skeleton.
 */
class CenterOfGravity {
public:
    CenterOfGravity();

    /**
     * @brief Initialize with skeleton reference
     * @param skeleton The player skeleton (to find spine_root)
     */
    void initialize(const RuntimeSkeleton* skeleton);

    /**
     * @brief Apply a force to the center of gravity
     * @param force Force vector in world space
     */
    void applyForce(const glm::vec3& force);

    /**
     * @brief Apply an impulse (instant velocity change)
     * @param impulse Impulse vector in world space
     */
    void applyImpulse(const glm::vec3& impulse);

    /**
     * @brief Get the world position of center of gravity
     */
    glm::vec3 getPosition() const { return m_position; }

    /**
     * @brief Set the world position
     */
    void setPosition(const glm::vec3& pos) { m_position = pos; }

    /**
     * @brief Get current velocity
     */
    glm::vec3 getVelocity() const { return m_velocity; }

    /**
     * @brief Set velocity
     */
    void setVelocity(const glm::vec3& vel) { m_velocity = vel; }

    /**
     * @brief Get the spine_root bone index
     */
    int getSpineRootIndex() const { return m_spineRootIndex; }

    /**
     * @brief Reset all physics state
     */
    void reset();

private:
    glm::vec3 m_position;           // World position of center of gravity
    glm::vec3 m_velocity;           // Current velocity
    glm::vec3 m_accumulatedForce;   // Accumulated forces for this frame
    float m_mass;                   // Mass for force calculations

    const RuntimeSkeleton* m_skeleton;
    int m_spineRootIndex;           // Cached index of spine_root bone
};

} // namespace PlayerPhysics
